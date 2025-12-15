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
#include "nm-vpn-sso-service.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>
#include <glib.h>
#include <glib-unix.h>
#include <NetworkManager.h>

#define NM_VPN_SSO_BUS_NAME "org.freedesktop.NetworkManager.vpn-sso"

/*
 * Set up the user's D-Bus session environment early.
 * This MUST be called before any GLib/D-Bus initialization so that
 * libsecret (and other D-Bus clients) can connect to the user's session.
 */
static void
setup_user_dbus_session (void)
{
    /* If we already have a D-Bus session, nothing to do */
    if (g_getenv ("DBUS_SESSION_BUS_ADDRESS"))
        return;

    /* If not running as root, nothing to do */
    if (getuid () != 0)
        return;

    fprintf (stderr, "[MESSAGE] Running as root without D-Bus session - detecting user session\n");

    VpnSsoSessionEnv *env = vpn_sso_get_graphical_session_env ();
    if (!env) {
        fprintf (stderr, "[WARNING] Could not detect user graphical session\n");
        return;
    }

    if (env->dbus_session_bus_address) {
        setenv ("DBUS_SESSION_BUS_ADDRESS", env->dbus_session_bus_address, 1);
        fprintf (stderr, "[MESSAGE] Set DBUS_SESSION_BUS_ADDRESS=%s\n", env->dbus_session_bus_address);
    }

    if (env->xdg_runtime_dir) {
        setenv ("XDG_RUNTIME_DIR", env->xdg_runtime_dir, 1);
        fprintf (stderr, "[MESSAGE] Set XDG_RUNTIME_DIR=%s\n", env->xdg_runtime_dir);
    }

    if (env->display) {
        setenv ("DISPLAY", env->display, 1);
    }

    if (env->home) {
        setenv ("HOME", env->home, 1);
    }

    vpn_sso_session_env_free (env);
}

static GMainLoop *main_loop = NULL;
static NmVpnSsoService *vpn_service = NULL;

static gboolean
signal_handler (gpointer user_data)
{
    g_message ("Received signal, shutting down");

    if (main_loop)
        g_main_loop_quit (main_loop);

    return G_SOURCE_REMOVE;
}

static void
quit_mainloop (NMVpnServicePlugin *plugin, gpointer user_data)
{
    g_message ("VPN service plugin quit signal received");

    if (main_loop)
        g_main_loop_quit (main_loop);
}

static void
log_handler (const gchar *log_domain,
             GLogLevelFlags log_level,
             const gchar *message,
             gpointer user_data)
{
    const char *level_str;

    switch (log_level & G_LOG_LEVEL_MASK) {
    case G_LOG_LEVEL_ERROR:
        level_str = "ERROR";
        break;
    case G_LOG_LEVEL_CRITICAL:
        level_str = "CRITICAL";
        break;
    case G_LOG_LEVEL_WARNING:
        level_str = "WARNING";
        break;
    case G_LOG_LEVEL_MESSAGE:
        level_str = "MESSAGE";
        break;
    case G_LOG_LEVEL_INFO:
        level_str = "INFO";
        break;
    case G_LOG_LEVEL_DEBUG:
        level_str = "DEBUG";
        break;
    default:
        level_str = "LOG";
        break;
    }

    /* Output to stderr for systemd journal integration */
    fprintf (stderr, "[%s] %s%s%s\n",
             level_str,
             log_domain ? log_domain : "",
             log_domain ? ": " : "",
             message);
}

int
main (int argc, char **argv)
{
    gboolean persist = FALSE;
    gboolean debug = FALSE;
    int ret = EXIT_SUCCESS;
    GOptionContext *opt_ctx;
    GError *error = NULL;

    GOptionEntry options[] = {
        { "persist", 0, 0, G_OPTION_ARG_NONE, &persist,
          "Don't quit when VPN connection terminates", NULL },
        { "debug", 0, 0, G_OPTION_ARG_NONE, &debug,
          "Enable verbose debug logging", NULL },
        { NULL }
    };

    /*
     * CRITICAL: Set up D-Bus session environment BEFORE any GLib initialization.
     * This ensures libsecret can connect to the user's keyring.
     */
    setup_user_dbus_session ();

    /* Set locale */
    setlocale (LC_ALL, "");

    /* Parse command line options */
    opt_ctx = g_option_context_new ("- GNOME VPN SSO service");
    g_option_context_add_main_entries (opt_ctx, options, NULL);
    g_option_context_set_summary (opt_ctx,
        "NetworkManager VPN plugin for OpenConnect with SSO authentication.\n"
        "Supports GlobalProtect (Palo Alto) and AnyConnect (Cisco) protocols.");

    if (!g_option_context_parse (opt_ctx, &argc, &argv, &error)) {
        g_printerr ("Error parsing options: %s\n", error->message);
        g_error_free (error);
        g_option_context_free (opt_ctx);
        return EXIT_FAILURE;
    }
    g_option_context_free (opt_ctx);

    /* Set up logging */
    g_log_set_handler (NULL,
                      G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
                      log_handler,
                      NULL);

    if (debug) {
        g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
        g_message ("Debug logging enabled");
    }

    g_message ("Starting GNOME VPN SSO service (version %s)", PACKAGE_VERSION);
    g_message ("Bus name: %s", NM_VPN_SSO_BUS_NAME);

    /* Create the VPN service */
    vpn_service = nm_vpn_sso_service_new (NM_VPN_SSO_BUS_NAME);
    if (!vpn_service) {
        g_printerr ("Failed to create VPN service\n");
        return EXIT_FAILURE;
    }

    /* Create main loop */
    main_loop = g_main_loop_new (NULL, FALSE);

    /* Set up signal handlers */
    g_unix_signal_add (SIGTERM, signal_handler, NULL);
    g_unix_signal_add (SIGINT, signal_handler, NULL);

    /* Connect to quit signal */
    if (!persist) {
        g_signal_connect (vpn_service, "quit",
                         G_CALLBACK (quit_mainloop),
                         NULL);
    }

    g_message ("VPN service ready, entering main loop");

    /* Run the main loop */
    g_main_loop_run (main_loop);

    g_message ("Main loop exited, cleaning up");

    /* Cleanup */
    if (vpn_service) {
        g_object_unref (vpn_service);
        vpn_service = NULL;
    }

    if (main_loop) {
        g_main_loop_unref (main_loop);
        main_loop = NULL;
    }

    g_message ("VPN service shutdown complete");

    return ret;
}
