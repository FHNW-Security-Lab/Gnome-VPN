/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (C) 2024 GNOME VPN SSO Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include "nm-vpn-sso-auth-dialog.h"

#include <gtk/gtk.h>
#include <adwaita.h>
#include <webkit/webkit.h>
#include <glib/gi18n.h>

struct _NmVpnSsoAuthDialog {
    AdwWindow parent_instance;

    /* Properties */
    char *gateway;
    char *protocol;
    char *cookie;
    char *username;

    /* UI Components */
    GtkWidget *header_bar;
    GtkWidget *status_page;
    GtkWidget *spinner;
    GtkWidget *web_view;
    GtkWidget *progress_bar;
    GtkWidget *cancel_button;

    /* State */
    gboolean auth_completed;
    gboolean auth_failed;
    GMainLoop *main_loop;
};

G_DEFINE_FINAL_TYPE (NmVpnSsoAuthDialog, nm_vpn_sso_auth_dialog, ADW_TYPE_WINDOW)

enum {
    PROP_0,
    PROP_GATEWAY,
    PROP_PROTOCOL,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Forward declarations */
static void setup_webview (NmVpnSsoAuthDialog *self);
static void on_load_changed (WebKitWebView           *web_view,
                              WebKitLoadEvent          load_event,
                              NmVpnSsoAuthDialog      *self);
static void on_cookie_changed (WebKitCookieManager *cookie_manager,
                                NmVpnSsoAuthDialog  *self);
static void on_load_progress (WebKitWebView       *web_view,
                               GParamSpec          *pspec,
                               NmVpnSsoAuthDialog  *self);
static gboolean on_load_failed (WebKitWebView       *web_view,
                                 WebKitLoadEvent      load_event,
                                 const char          *failing_uri,
                                 GError              *error,
                                 NmVpnSsoAuthDialog  *self);

/* Implementation */

static void
nm_vpn_sso_auth_dialog_finalize (GObject *object)
{
    NmVpnSsoAuthDialog *self = NM_VPN_SSO_AUTH_DIALOG (object);

    g_clear_pointer (&self->gateway, g_free);
    g_clear_pointer (&self->protocol, g_free);
    g_clear_pointer (&self->cookie, g_free);
    g_clear_pointer (&self->username, g_free);

    if (self->main_loop) {
        g_main_loop_unref (self->main_loop);
        self->main_loop = NULL;
    }

    G_OBJECT_CLASS (nm_vpn_sso_auth_dialog_parent_class)->finalize (object);
}

static void
nm_vpn_sso_auth_dialog_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
    NmVpnSsoAuthDialog *self = NM_VPN_SSO_AUTH_DIALOG (object);

    switch (prop_id) {
    case PROP_GATEWAY:
        g_value_set_string (value, self->gateway);
        break;
    case PROP_PROTOCOL:
        g_value_set_string (value, self->protocol);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
nm_vpn_sso_auth_dialog_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    NmVpnSsoAuthDialog *self = NM_VPN_SSO_AUTH_DIALOG (object);

    switch (prop_id) {
    case PROP_GATEWAY:
        g_free (self->gateway);
        self->gateway = g_value_dup_string (value);
        break;
    case PROP_PROTOCOL:
        g_free (self->protocol);
        self->protocol = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
on_cancel_clicked (GtkButton *button, NmVpnSsoAuthDialog *self)
{
    g_debug ("Authentication cancelled by user");
    self->auth_failed = TRUE;

    if (self->main_loop && g_main_loop_is_running (self->main_loop))
        g_main_loop_quit (self->main_loop);

    gtk_window_close (GTK_WINDOW (self));
}

static void
nm_vpn_sso_auth_dialog_constructed (GObject *object)
{
    NmVpnSsoAuthDialog *self = NM_VPN_SSO_AUTH_DIALOG (object);

    G_OBJECT_CLASS (nm_vpn_sso_auth_dialog_parent_class)->constructed (object);

    setup_webview (self);
}

static void
nm_vpn_sso_auth_dialog_init (NmVpnSsoAuthDialog *self)
{
    GtkWidget *main_box;
    GtkWidget *toolbar_view;

    self->auth_completed = FALSE;
    self->auth_failed = FALSE;
    self->main_loop = g_main_loop_new (NULL, FALSE);

    /* Set window properties */
    gtk_window_set_title (GTK_WINDOW (self), _("VPN Authentication"));
    gtk_window_set_default_size (GTK_WINDOW (self), 800, 600);
    gtk_window_set_modal (GTK_WINDOW (self), TRUE);

    /* Create toolbar view */
    toolbar_view = adw_toolbar_view_new ();
    gtk_window_set_child (GTK_WINDOW (self), toolbar_view);

    /* Create header bar */
    self->header_bar = adw_header_bar_new ();
    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar_view), self->header_bar);

    /* Add cancel button to header */
    self->cancel_button = gtk_button_new_with_label (_("Cancel"));
    adw_header_bar_pack_start (ADW_HEADER_BAR (self->header_bar), self->cancel_button);
    g_signal_connect (self->cancel_button, "clicked", G_CALLBACK (on_cancel_clicked), self);

    /* Create main box */
    main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar_view), main_box);

    /* Create progress bar */
    self->progress_bar = gtk_progress_bar_new ();
    gtk_widget_set_visible (self->progress_bar, FALSE);
    gtk_box_append (GTK_BOX (main_box), self->progress_bar);

    /* Create status page (shown initially) */
    self->status_page = adw_status_page_new ();
    adw_status_page_set_icon_name (ADW_STATUS_PAGE (self->status_page), "network-vpn-symbolic");
    adw_status_page_set_title (ADW_STATUS_PAGE (self->status_page), _("Authenticating"));
    adw_status_page_set_description (ADW_STATUS_PAGE (self->status_page),
                                      _("Please wait while we load the authentication page"));

    self->spinner = gtk_spinner_new ();
    gtk_spinner_start (GTK_SPINNER (self->spinner));
    adw_status_page_set_child (ADW_STATUS_PAGE (self->status_page), self->spinner);

    gtk_box_append (GTK_BOX (main_box), self->status_page);

    g_debug ("VPN SSO auth dialog initialized");
}

static void
nm_vpn_sso_auth_dialog_class_init (NmVpnSsoAuthDialogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = nm_vpn_sso_auth_dialog_finalize;
    object_class->get_property = nm_vpn_sso_auth_dialog_get_property;
    object_class->set_property = nm_vpn_sso_auth_dialog_set_property;
    object_class->constructed = nm_vpn_sso_auth_dialog_constructed;

    properties[PROP_GATEWAY] =
        g_param_spec_string ("gateway",
                             "Gateway",
                             "VPN gateway URL",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    properties[PROP_PROTOCOL] =
        g_param_spec_string ("protocol",
                             "Protocol",
                             "VPN protocol (globalprotect or anyconnect)",
                             "globalprotect",
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
extract_globalprotect_cookie (NmVpnSsoAuthDialog *self, const char *cookie_value)
{
    /* GlobalProtect uses a prelogin.esp cookie or portal-userauthcookie */
    if (g_str_has_prefix (cookie_value, "authcookie=") ||
        g_str_has_prefix (cookie_value, "portal-userauthcookie=")) {
        const char *start = strchr (cookie_value, '=');
        if (start) {
            start++; /* Skip the '=' */
            char *cookie = g_strdup (start);
            /* Remove any trailing attributes */
            char *semicolon = strchr (cookie, ';');
            if (semicolon)
                *semicolon = '\0';

            g_free (self->cookie);
            self->cookie = cookie;
            g_debug ("Captured GlobalProtect cookie: %s", self->cookie);
        }
    }
}

static void
extract_anyconnect_cookie (NmVpnSsoAuthDialog *self, const char *cookie_value)
{
    /* AnyConnect uses webvpn or webvpnlogin cookies */
    if (g_str_has_prefix (cookie_value, "webvpn=") ||
        g_str_has_prefix (cookie_value, "webvpnlogin=")) {
        const char *start = strchr (cookie_value, '=');
        if (start) {
            start++; /* Skip the '=' */
            char *cookie = g_strdup (start);
            /* Remove any trailing attributes */
            char *semicolon = strchr (cookie, ';');
            if (semicolon)
                *semicolon = '\0';

            g_free (self->cookie);
            self->cookie = cookie;
            g_debug ("Captured AnyConnect cookie: %s", self->cookie);
        }
    }
}

static void
on_cookies_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    NmVpnSsoAuthDialog *self = NM_VPN_SSO_AUTH_DIALOG (user_data);
    WebKitCookieManager *cookie_manager = WEBKIT_COOKIE_MANAGER (source_object);
    GList *cookies, *l;

    cookies = webkit_cookie_manager_get_cookies_finish (cookie_manager, result, NULL);

    for (l = cookies; l != NULL; l = l->next) {
        SoupCookie *cookie = l->data;
        const char *name = soup_cookie_get_name (cookie);
        const char *value = soup_cookie_get_value (cookie);

        g_debug ("Cookie found: %s=%s", name, value);

        /* Check for authentication cookies based on protocol */
        if (g_strcmp0 (self->protocol, "globalprotect") == 0) {
            if (g_strcmp0 (name, "authcookie") == 0 ||
                g_strcmp0 (name, "portal-userauthcookie") == 0) {
                g_free (self->cookie);
                self->cookie = g_strdup (value);
                g_debug ("Captured GlobalProtect cookie: %s", self->cookie);
                self->auth_completed = TRUE;
            }
        } else if (g_strcmp0 (self->protocol, "anyconnect") == 0) {
            if (g_strcmp0 (name, "webvpn") == 0 ||
                g_strcmp0 (name, "webvpnlogin") == 0) {
                g_free (self->cookie);
                self->cookie = g_strdup (value);
                g_debug ("Captured AnyConnect cookie: %s", self->cookie);
                self->auth_completed = TRUE;
            }
        }

        soup_cookie_free (cookie);
    }

    g_list_free (cookies);

    /* If authentication completed, close the dialog */
    if (self->auth_completed) {
        g_debug ("Authentication completed successfully");
        if (self->main_loop && g_main_loop_is_running (self->main_loop))
            g_main_loop_quit (self->main_loop);
        gtk_window_close (GTK_WINDOW (self));
    }
}

static void
check_authentication_cookies (NmVpnSsoAuthDialog *self)
{
    WebKitNetworkSession *session;
    WebKitCookieManager *cookie_manager;

    session = webkit_web_view_get_network_session (WEBKIT_WEB_VIEW (self->web_view));
    cookie_manager = webkit_network_session_get_cookie_manager (session);

    webkit_cookie_manager_get_cookies (cookie_manager,
                                        self->gateway,
                                        NULL,
                                        on_cookies_ready,
                                        self);
}

static void
on_load_changed (WebKitWebView      *web_view,
                  WebKitLoadEvent     load_event,
                  NmVpnSsoAuthDialog *self)
{
    const char *uri;

    switch (load_event) {
    case WEBKIT_LOAD_STARTED:
        g_debug ("Page load started");
        gtk_widget_set_visible (self->progress_bar, TRUE);
        break;

    case WEBKIT_LOAD_REDIRECTED:
        uri = webkit_web_view_get_uri (web_view);
        g_debug ("Page redirected to: %s", uri);
        break;

    case WEBKIT_LOAD_COMMITTED:
        uri = webkit_web_view_get_uri (web_view);
        g_debug ("Page committed: %s", uri);

        /* Hide status page, show web view */
        if (gtk_widget_get_visible (self->status_page)) {
            gtk_widget_set_visible (self->status_page, FALSE);
            gtk_widget_set_visible (self->web_view, TRUE);
        }

        /* Check for authentication completion based on URL patterns */
        if (g_strcmp0 (self->protocol, "globalprotect") == 0) {
            if (strstr (uri, "/global-protect/") || strstr (uri, "/ssl-vpn/")) {
                check_authentication_cookies (self);
            }
        } else if (g_strcmp0 (self->protocol, "anyconnect") == 0) {
            if (strstr (uri, "/+CSCOE+/") || strstr (uri, "/+webvpn+/")) {
                check_authentication_cookies (self);
            }
        }
        break;

    case WEBKIT_LOAD_FINISHED:
        g_debug ("Page load finished");
        gtk_widget_set_visible (self->progress_bar, FALSE);
        gtk_spinner_stop (GTK_SPINNER (self->spinner));

        /* Final cookie check */
        check_authentication_cookies (self);
        break;
    }
}

static gboolean
on_load_failed (WebKitWebView      *web_view,
                WebKitLoadEvent     load_event,
                const char         *failing_uri,
                GError             *error,
                NmVpnSsoAuthDialog *self)
{
    g_warning ("Failed to load page: %s", error->message);

    adw_status_page_set_icon_name (ADW_STATUS_PAGE (self->status_page), "dialog-error-symbolic");
    adw_status_page_set_title (ADW_STATUS_PAGE (self->status_page), _("Authentication Failed"));
    adw_status_page_set_description (ADW_STATUS_PAGE (self->status_page), error->message);

    gtk_widget_set_visible (self->web_view, FALSE);
    gtk_widget_set_visible (self->status_page, TRUE);
    gtk_spinner_stop (GTK_SPINNER (self->spinner));
    gtk_widget_set_visible (self->progress_bar, FALSE);

    self->auth_failed = TRUE;

    return TRUE; /* Error handled */
}

static void
on_load_progress (WebKitWebView      *web_view,
                  GParamSpec         *pspec,
                  NmVpnSsoAuthDialog *self)
{
    double progress = webkit_web_view_get_estimated_load_progress (web_view);
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress_bar), progress);
}

static void
setup_webview (NmVpnSsoAuthDialog *self)
{
    WebKitNetworkSession *network_session;
    WebKitCookieManager *cookie_manager;
    WebKitSettings *settings;
    char *url;

    /* Create network session - use ephemeral for privacy */
    network_session = webkit_network_session_new_ephemeral ();

    /* Configure cookie manager */
    cookie_manager = webkit_network_session_get_cookie_manager (network_session);
    webkit_cookie_manager_set_accept_policy (cookie_manager,
                                              WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);

    /* Create web view with network session */
    self->web_view = g_object_new (WEBKIT_TYPE_WEB_VIEW,
                                    "network-session", network_session,
                                    NULL);
    gtk_widget_set_visible (self->web_view, FALSE);
    gtk_widget_set_vexpand (self->web_view, TRUE);

    /* Configure WebKit settings */
    settings = webkit_web_view_get_settings (WEBKIT_WEB_VIEW (self->web_view));
    webkit_settings_set_enable_javascript (settings, TRUE);
    webkit_settings_set_enable_webgl (settings, FALSE);
    /* plugins are no longer supported in WebKitGTK 6.0 */
    webkit_settings_set_user_agent (settings, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");

    /* Connect signals */
    g_signal_connect (self->web_view, "load-changed",
                      G_CALLBACK (on_load_changed), self);
    g_signal_connect (self->web_view, "load-failed",
                      G_CALLBACK (on_load_failed), self);
    g_signal_connect (self->web_view, "notify::estimated-load-progress",
                      G_CALLBACK (on_load_progress), self);

    /* Add web view to UI - find the main box and append */
    GtkWidget *toolbar_view = gtk_window_get_child (GTK_WINDOW (self));
    GtkWidget *main_box = adw_toolbar_view_get_content (ADW_TOOLBAR_VIEW (toolbar_view));
    gtk_box_append (GTK_BOX (main_box), self->web_view);

    /* Build URL based on protocol */
    if (g_strcmp0 (self->protocol, "globalprotect") == 0) {
        url = g_strdup_printf ("https://%s/global-protect/prelogin.esp", self->gateway);
    } else {
        url = g_strdup_printf ("https://%s/", self->gateway);
    }

    g_debug ("Loading authentication URL: %s", url);
    webkit_web_view_load_uri (WEBKIT_WEB_VIEW (self->web_view), url);

    g_free (url);
}

/* Public API */

NmVpnSsoAuthDialog *
nm_vpn_sso_auth_dialog_new (const char *gateway,
                             const char *protocol)
{
    g_return_val_if_fail (gateway != NULL, NULL);
    g_return_val_if_fail (protocol != NULL, NULL);

    return g_object_new (NM_TYPE_VPN_SSO_AUTH_DIALOG,
                         "gateway", gateway,
                         "protocol", protocol,
                         NULL);
}

const char *
nm_vpn_sso_auth_dialog_get_cookie (NmVpnSsoAuthDialog *self)
{
    g_return_val_if_fail (NM_IS_VPN_SSO_AUTH_DIALOG (self), NULL);
    return self->cookie;
}

const char *
nm_vpn_sso_auth_dialog_get_username (NmVpnSsoAuthDialog *self)
{
    g_return_val_if_fail (NM_IS_VPN_SSO_AUTH_DIALOG (self), NULL);
    return self->username;
}
