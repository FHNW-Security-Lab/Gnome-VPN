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
#include "nm-vpn-sso-editor.h"

#include <gtk/gtk.h>
#include <adwaita.h>
#include <NetworkManager.h>
#include <string.h>
#include <libintl.h>

#include "../shared/vpn-config.h"

/*****************************************************************************/
/* Editor Widget Implementation                                              */
/*****************************************************************************/

struct _NmVpnSsoEditor {
    GObject parent;

    GtkWidget *widget;
    GtkWidget *gateway_entry;
    GtkWidget *protocol_combo;
    GtkWidget *username_entry;
    GtkWidget *cache_hours_spin;
    GtkWidget *external_browser_check;
    GtkWidget *extra_args_entry;

    NMConnection *connection;
    gboolean changed;
};

static void nm_vpn_sso_editor_interface_init (NMVpnEditorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (NmVpnSsoEditor, nm_vpn_sso_editor, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (NM_TYPE_VPN_EDITOR,
                                                nm_vpn_sso_editor_interface_init))

static void
widget_changed_cb (GtkWidget *widget, gpointer user_data)
{
    NmVpnSsoEditor *self = NM_VPN_SSO_EDITOR (user_data);

    self->changed = TRUE;
    g_signal_emit_by_name (self, "changed");
}

static void
init_editor_ui (NmVpnSsoEditor *self)
{
    GtkWidget *grid;
    GtkWidget *label;
    int row = 0;

    /* Create main container */
    self->widget = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top (self->widget, 12);
    gtk_widget_set_margin_bottom (self->widget, 12);
    gtk_widget_set_margin_start (self->widget, 12);
    gtk_widget_set_margin_end (self->widget, 12);

    /* Create settings grid */
    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_box_append (GTK_BOX (self->widget), grid);

    /* Gateway */
    label = gtk_label_new ("Gateway:");
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

    self->gateway_entry = gtk_entry_new ();
    gtk_widget_set_hexpand (self->gateway_entry, TRUE);
    gtk_entry_set_placeholder_text (GTK_ENTRY (self->gateway_entry), "vpn.example.com");
    gtk_grid_attach (GTK_GRID (grid), self->gateway_entry, 1, row, 1, 1);
    g_signal_connect (self->gateway_entry, "changed", G_CALLBACK (widget_changed_cb), self);
    row++;

    /* Protocol */
    label = gtk_label_new ("Protocol:");
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

    self->protocol_combo = gtk_drop_down_new_from_strings ((const char *[]) {
        "GlobalProtect",
        "AnyConnect",
        NULL
    });
    gtk_widget_set_hexpand (self->protocol_combo, TRUE);
    gtk_grid_attach (GTK_GRID (grid), self->protocol_combo, 1, row, 1, 1);
    g_signal_connect (self->protocol_combo, "notify::selected", G_CALLBACK (widget_changed_cb), self);
    row++;

    /* Username */
    label = gtk_label_new ("Username:");
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

    self->username_entry = gtk_entry_new ();
    gtk_widget_set_hexpand (self->username_entry, TRUE);
    gtk_entry_set_placeholder_text (GTK_ENTRY (self->username_entry), "user@example.com (optional)");
    gtk_grid_attach (GTK_GRID (grid), self->username_entry, 1, row, 1, 1);
    g_signal_connect (self->username_entry, "changed", G_CALLBACK (widget_changed_cb), self);
    row++;

    /* Cache duration */
    label = gtk_label_new ("Cache Duration:");
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

    GtkWidget *cache_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    self->cache_hours_spin = gtk_spin_button_new_with_range (0, 168, 1);  /* 0-168 hours (1 week) */
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->cache_hours_spin), 8);  /* Default 8 hours */
    gtk_widget_set_tooltip_text (self->cache_hours_spin,
        "How long to cache SSO credentials (0 = always require fresh SSO)");
    gtk_box_append (GTK_BOX (cache_box), self->cache_hours_spin);

    GtkWidget *hours_label = gtk_label_new ("hours");
    gtk_widget_add_css_class (hours_label, "dim-label");
    gtk_box_append (GTK_BOX (cache_box), hours_label);

    gtk_widget_set_hexpand (cache_box, TRUE);
    gtk_grid_attach (GTK_GRID (grid), cache_box, 1, row, 1, 1);
    g_signal_connect (self->cache_hours_spin, "value-changed", G_CALLBACK (widget_changed_cb), self);
    row++;

    /* External browser checkbox */
    label = gtk_label_new ("External Browser:");
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

    self->external_browser_check = gtk_check_button_new_with_label (
        "Use system browser for SSO (enables password manager)");
    gtk_widget_set_tooltip_text (self->external_browser_check,
        "Opens your default browser (Firefox, Chrome, etc.) for SSO login instead of embedded browser.\n"
        "This allows you to use your password manager extensions.");
    gtk_widget_set_hexpand (self->external_browser_check, TRUE);
    gtk_grid_attach (GTK_GRID (grid), self->external_browser_check, 1, row, 1, 1);
    g_signal_connect (self->external_browser_check, "toggled", G_CALLBACK (widget_changed_cb), self);
    row++;

    /* Extra arguments */
    label = gtk_label_new ("Extra Arguments:");
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_widget_set_valign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

    self->extra_args_entry = gtk_entry_new ();
    gtk_widget_set_hexpand (self->extra_args_entry, TRUE);
    gtk_entry_set_placeholder_text (GTK_ENTRY (self->extra_args_entry), "--os=linux-64 (optional)");
    gtk_grid_attach (GTK_GRID (grid), self->extra_args_entry, 1, row, 1, 1);
    g_signal_connect (self->extra_args_entry, "changed", G_CALLBACK (widget_changed_cb), self);

    /* Add help text */
    GtkWidget *help_label = gtk_label_new (
        "This VPN uses Single Sign-On (SSO) authentication.\n"
        "A browser window will open when connecting.\n"
        "Credentials are cached securely to allow quick reconnection.");
    gtk_widget_set_margin_top (help_label, 12);
    gtk_widget_add_css_class (help_label, "dim-label");
    gtk_box_append (GTK_BOX (self->widget), help_label);
}

static void
load_connection_settings (NmVpnSsoEditor *self)
{
    NMSettingVpn *s_vpn;
    const char *value;

    if (!self->connection)
        return;

    s_vpn = nm_connection_get_setting_vpn (self->connection);
    if (!s_vpn)
        return;

    /* Load gateway */
    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_GATEWAY);
    if (value)
        gtk_editable_set_text (GTK_EDITABLE (self->gateway_entry), value);

    /* Load protocol */
    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_PROTOCOL);
    if (value) {
        if (g_strcmp0 (value, NM_VPN_SSO_PROTOCOL_GLOBALPROTECT) == 0)
            gtk_drop_down_set_selected (GTK_DROP_DOWN (self->protocol_combo), 0);
        else if (g_strcmp0 (value, NM_VPN_SSO_PROTOCOL_ANYCONNECT) == 0)
            gtk_drop_down_set_selected (GTK_DROP_DOWN (self->protocol_combo), 1);
    }

    /* Load username */
    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_USERNAME);
    if (value)
        gtk_editable_set_text (GTK_EDITABLE (self->username_entry), value);

    /* Load cache duration */
    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_CACHE_HOURS);
    if (value)
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->cache_hours_spin), atoi (value));
    else
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->cache_hours_spin), 8);  /* Default */

    /* Load external browser */
    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_EXTERNAL_BROWSER);
    gtk_check_button_set_active (GTK_CHECK_BUTTON (self->external_browser_check),
                                  value && g_strcmp0 (value, "yes") == 0);

    /* Load extra arguments */
    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_EXTRA_ARGS);
    if (value)
        gtk_editable_set_text (GTK_EDITABLE (self->extra_args_entry), value);

    self->changed = FALSE;
}

static GObject *
nm_vpn_sso_editor_get_widget (NMVpnEditor *editor)
{
    NmVpnSsoEditor *self = NM_VPN_SSO_EDITOR (editor);

    return G_OBJECT (self->widget);
}

static gboolean
nm_vpn_sso_editor_update_connection (NMVpnEditor  *editor,
                                     NMConnection *connection,
                                     GError      **error)
{
    NmVpnSsoEditor *self = NM_VPN_SSO_EDITOR (editor);
    NMSettingVpn *s_vpn;
    const char *gateway;
    const char *username;
    const char *extra_args;
    const char *protocol;
    guint selected;

    if (!self->changed)
        return TRUE;

    s_vpn = nm_connection_get_setting_vpn (connection);
    if (!s_vpn) {
        s_vpn = NM_SETTING_VPN (nm_setting_vpn_new ());
        nm_connection_add_setting (connection, NM_SETTING (s_vpn));
    }

    /* Set service type */
    g_object_set (s_vpn, NM_SETTING_VPN_SERVICE_TYPE, NM_DBUS_SERVICE_VPN_SSO, NULL);

    /* Get and validate gateway */
    gateway = gtk_editable_get_text (GTK_EDITABLE (self->gateway_entry));
    if (!gateway || !*gateway) {
        g_set_error_literal (error,
                           NM_CONNECTION_ERROR,
                           NM_CONNECTION_ERROR_INVALID_PROPERTY,
                           "Gateway cannot be empty");
        return FALSE;
    }

    nm_setting_vpn_add_data_item (s_vpn, NM_VPN_SSO_KEY_GATEWAY, gateway);

    /* Get protocol */
    selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->protocol_combo));
    if (selected == 0)
        protocol = NM_VPN_SSO_PROTOCOL_GLOBALPROTECT;
    else
        protocol = NM_VPN_SSO_PROTOCOL_ANYCONNECT;

    nm_setting_vpn_add_data_item (s_vpn, NM_VPN_SSO_KEY_PROTOCOL, protocol);

    /* Get username (optional) */
    username = gtk_editable_get_text (GTK_EDITABLE (self->username_entry));
    if (username && *username)
        nm_setting_vpn_add_data_item (s_vpn, NM_VPN_SSO_KEY_USERNAME, username);
    else
        nm_setting_vpn_remove_data_item (s_vpn, NM_VPN_SSO_KEY_USERNAME);

    /* Get cache duration */
    gint cache_hours = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (self->cache_hours_spin));
    if (cache_hours > 0) {
        g_autofree gchar *cache_str = g_strdup_printf ("%d", cache_hours);
        nm_setting_vpn_add_data_item (s_vpn, NM_VPN_SSO_KEY_CACHE_HOURS, cache_str);
    } else {
        nm_setting_vpn_remove_data_item (s_vpn, NM_VPN_SSO_KEY_CACHE_HOURS);
    }

    /* Get external browser (optional, for GlobalProtect) */
    if (gtk_check_button_get_active (GTK_CHECK_BUTTON (self->external_browser_check)))
        nm_setting_vpn_add_data_item (s_vpn, NM_VPN_SSO_KEY_EXTERNAL_BROWSER, "yes");
    else
        nm_setting_vpn_remove_data_item (s_vpn, NM_VPN_SSO_KEY_EXTERNAL_BROWSER);

    /* Get extra arguments (optional) */
    extra_args = gtk_editable_get_text (GTK_EDITABLE (self->extra_args_entry));
    if (extra_args && *extra_args)
        nm_setting_vpn_add_data_item (s_vpn, NM_VPN_SSO_KEY_EXTRA_ARGS, extra_args);
    else
        nm_setting_vpn_remove_data_item (s_vpn, NM_VPN_SSO_KEY_EXTRA_ARGS);

    self->changed = FALSE;
    return TRUE;
}

static void
nm_vpn_sso_editor_interface_init (NMVpnEditorInterface *iface)
{
    iface->get_widget = nm_vpn_sso_editor_get_widget;
    iface->update_connection = nm_vpn_sso_editor_update_connection;
}

static void
nm_vpn_sso_editor_init (NmVpnSsoEditor *self)
{
    self->changed = FALSE;
}

static void
nm_vpn_sso_editor_dispose (GObject *object)
{
    NmVpnSsoEditor *self = NM_VPN_SSO_EDITOR (object);

    g_clear_object (&self->connection);
    g_clear_pointer (&self->widget, gtk_widget_unparent);

    G_OBJECT_CLASS (nm_vpn_sso_editor_parent_class)->dispose (object);
}

static void
nm_vpn_sso_editor_class_init (NmVpnSsoEditorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = nm_vpn_sso_editor_dispose;
}

NMVpnEditor *
nm_vpn_sso_editor_new (NMConnection *connection)
{
    NmVpnSsoEditor *self;

    self = g_object_new (NM_TYPE_VPN_SSO_EDITOR, NULL);

    if (connection)
        self->connection = g_object_ref (connection);

    init_editor_ui (self);
    load_connection_settings (self);

    return NM_VPN_EDITOR (self);
}

/*****************************************************************************/
/* Editor Plugin Implementation                                              */
/*****************************************************************************/

struct _NmVpnSsoEditorPlugin {
    GObject parent;
};

struct _NmVpnSsoEditorPluginClass {
    GObjectClass parent;
};

enum {
    PROP_PLUGIN_0,
    PROP_PLUGIN_NAME,
    PROP_PLUGIN_DESC,
    PROP_PLUGIN_SERVICE,
    LAST_PLUGIN_PROP
};

static void nm_vpn_sso_editor_plugin_interface_init (NMVpnEditorPluginInterface *iface);

G_DEFINE_TYPE_WITH_CODE (NmVpnSsoEditorPlugin, nm_vpn_sso_editor_plugin, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (NM_TYPE_VPN_EDITOR_PLUGIN,
                                                nm_vpn_sso_editor_plugin_interface_init))

static NMVpnEditor *
nm_vpn_sso_editor_plugin_get_editor (NMVpnEditorPlugin  *plugin,
                                     NMConnection       *connection,
                                     GError            **error)
{
    return nm_vpn_sso_editor_new (connection);
}

static NMVpnEditorPluginCapability
nm_vpn_sso_editor_plugin_get_capabilities (NMVpnEditorPlugin *plugin)
{
    return NM_VPN_EDITOR_PLUGIN_CAPABILITY_IPV6;
}

static void
nm_vpn_sso_editor_plugin_interface_init (NMVpnEditorPluginInterface *iface)
{
    iface->get_editor = nm_vpn_sso_editor_plugin_get_editor;
    iface->get_capabilities = nm_vpn_sso_editor_plugin_get_capabilities;
}

static void
nm_vpn_sso_editor_plugin_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    switch (prop_id) {
    case PROP_PLUGIN_NAME:
        g_value_set_string (value, "SSO VPN (GlobalProtect/AnyConnect)");
        break;
    case PROP_PLUGIN_DESC:
        g_value_set_string (value, "Compatible with GlobalProtect and Cisco AnyConnect SSO VPNs");
        break;
    case PROP_PLUGIN_SERVICE:
        g_value_set_string (value, NM_DBUS_SERVICE_VPN_SSO);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
nm_vpn_sso_editor_plugin_init (NmVpnSsoEditorPlugin *self)
{
}

static void
nm_vpn_sso_editor_plugin_class_init (NmVpnSsoEditorPluginClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = nm_vpn_sso_editor_plugin_get_property;

    g_object_class_override_property (object_class, PROP_PLUGIN_NAME, NM_VPN_EDITOR_PLUGIN_NAME);
    g_object_class_override_property (object_class, PROP_PLUGIN_DESC, NM_VPN_EDITOR_PLUGIN_DESCRIPTION);
    g_object_class_override_property (object_class, PROP_PLUGIN_SERVICE, NM_VPN_EDITOR_PLUGIN_SERVICE);
}

/*****************************************************************************/
/* Plugin Factory Function - Called by NetworkManager                       */
/*****************************************************************************/

G_MODULE_EXPORT NMVpnEditorPlugin *
nm_vpn_editor_plugin_factory (GError **error)
{
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

    return g_object_new (NM_TYPE_VPN_SSO_EDITOR_PLUGIN, NULL);
}
