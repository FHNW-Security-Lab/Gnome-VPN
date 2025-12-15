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

#include <stdlib.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib/gi18n.h>

static char *opt_gateway = NULL;
static char *opt_protocol = NULL;
static gboolean opt_version = FALSE;

static GOptionEntry option_entries[] = {
    { "gateway", 'g', 0, G_OPTION_ARG_STRING, &opt_gateway,
      N_("VPN gateway URL"), N_("URL") },
    { "protocol", 'p', 0, G_OPTION_ARG_STRING, &opt_protocol,
      N_("VPN protocol (globalprotect or anyconnect)"), N_("PROTOCOL") },
    { "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version,
      N_("Show version information"), NULL },
    { NULL }
};

static void
on_window_close_request (GtkWindow *window, gpointer user_data)
{
    NmVpnSsoAuthDialog *dialog = NM_VPN_SSO_AUTH_DIALOG (window);
    const char *cookie;

    /* Output the authentication result to stdout */
    cookie = nm_vpn_sso_auth_dialog_get_cookie (dialog);

    if (cookie != NULL) {
        g_print ("COOKIE=%s\n", cookie);

        const char *username = nm_vpn_sso_auth_dialog_get_username (dialog);
        if (username != NULL) {
            g_print ("USERNAME=%s\n", username);
        }

        exit (0);
    } else {
        g_printerr ("Authentication failed or cancelled\n");
        exit (1);
    }
}

static void
on_activate (AdwApplication *app, gpointer user_data)
{
    NmVpnSsoAuthDialog *dialog;
    const char *gateway;
    const char *protocol;

    /* Use provided options or defaults */
    gateway = opt_gateway ? opt_gateway : "vpn.example.com";
    protocol = opt_protocol ? opt_protocol : "globalprotect";

    /* Validate protocol */
    if (g_strcmp0 (protocol, "globalprotect") != 0 &&
        g_strcmp0 (protocol, "anyconnect") != 0) {
        g_printerr ("Invalid protocol '%s'. Must be 'globalprotect' or 'anyconnect'\n", protocol);
        exit (1);
    }

    /* Create and show dialog */
    dialog = nm_vpn_sso_auth_dialog_new (gateway, protocol);

    /* Connect close signal */
    g_signal_connect (dialog, "close-request",
                      G_CALLBACK (on_window_close_request), NULL);

    gtk_window_set_application (GTK_WINDOW (dialog), GTK_APPLICATION (app));
    gtk_window_present (GTK_WINDOW (dialog));
}

int
main (int argc, char **argv)
{
    AdwApplication *app;
    GOptionContext *context;
    GError *error = NULL;
    int status;

    /* Set up internationalization */
#ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    /* Parse command line options */
    context = g_option_context_new (_("- VPN SSO Authentication Dialog"));
    g_option_context_add_main_entries (context, option_entries, GETTEXT_PACKAGE);
    /* GTK4 doesn't have gtk_get_option_group - GTK initializes automatically via GtkApplication */

    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_printerr ("Error parsing command line options: %s\n", error->message);
        g_error_free (error);
        g_option_context_free (context);
        return 1;
    }

    g_option_context_free (context);

    /* Handle version option */
    if (opt_version) {
        g_print ("nm-vpn-sso-auth-dialog version %s\n", PACKAGE_VERSION);
        return 0;
    }

    /* Validate required options */
    if (opt_gateway == NULL) {
        g_printerr ("Error: --gateway option is required\n");
        return 1;
    }

    /* Create application */
    app = adw_application_new ("org.gnome.VpnSso.AuthDialog",
                               G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);

    /* Run application */
    status = g_application_run (G_APPLICATION (app), 0, NULL);

    /* Cleanup */
    g_object_unref (app);
    g_free (opt_gateway);
    g_free (opt_protocol);

    return status;
}
