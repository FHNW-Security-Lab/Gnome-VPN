/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Copyright (C) 2024 GNOME VPN SSO Contributors
 *
 * Example program demonstrating OcRunner usage.
 * This is for testing/development only, not part of the final package.
 *
 * Compile:
 *   gcc -o oc-runner-example openconnect-runner-example.c openconnect-runner.c \
 *       `pkg-config --cflags --libs gio-2.0 libnm` -I../shared
 *
 * Usage:
 *   ./oc-runner-example --protocol=gp --gateway=vpn.example.com \
 *       --username=user@example.com --cookie="<SSO_COOKIE>"
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "openconnect-runner.h"

static GMainLoop *main_loop = NULL;
static OcRunner *runner = NULL;

static void
signal_handler (int signum)
{
    g_print ("\nReceived signal %d, disconnecting...\n", signum);
    if (runner) {
        oc_runner_disconnect (runner);
    }
    if (main_loop) {
        g_main_loop_quit (main_loop);
    }
}

static void
on_state_changed (OcRunner *runner, OcRunnerState state, gpointer user_data)
{
    const char *state_str = oc_runner_state_to_string (state);
    g_print ("═══ State changed: %s ═══\n", state_str);

    if (state == OC_RUNNER_STATE_FAILED) {
        g_print ("Connection failed, exiting...\n");
        if (main_loop)
            g_main_loop_quit (main_loop);
    } else if (state == OC_RUNNER_STATE_IDLE && user_data) {
        g_print ("Disconnected, exiting...\n");
        if (main_loop)
            g_main_loop_quit (main_loop);
    }
}

static void
on_tunnel_ready (OcRunner *runner,
                 const char *ip4,
                 const char *ip6,
                 GHashTable *config,
                 gpointer user_data)
{
    GHashTableIter iter;
    gpointer key, value;

    g_print ("\n");
    g_print ("╔════════════════════════════════════════════╗\n");
    g_print ("║       VPN TUNNEL READY                     ║\n");
    g_print ("╚════════════════════════════════════════════╝\n");
    g_print ("\n");

    if (ip4)
        g_print ("  IPv4 Address: %s\n", ip4);
    if (ip6)
        g_print ("  IPv6 Address: %s\n", ip6);

    if (config && g_hash_table_size (config) > 0) {
        g_print ("\n  Configuration:\n");
        g_hash_table_iter_init (&iter, config);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
            g_print ("    %-20s: %s\n", (char *)key, (char *)value);
        }
    }

    g_print ("\n");
    g_print ("Press Ctrl+C to disconnect...\n");
    g_print ("\n");
}

static void
on_log_message (OcRunner *runner, const char *message, gpointer user_data)
{
    g_print ("│ %s\n", message);
}

static void
on_error_occurred (OcRunner *runner, GError *error, gpointer user_data)
{
    g_printerr ("\n");
    g_printerr ("╔════════════════════════════════════════════╗\n");
    g_printerr ("║       ERROR OCCURRED                       ║\n");
    g_printerr ("╚════════════════════════════════════════════╝\n");
    g_printerr ("\n");
    g_printerr ("  %s\n", error->message);
    g_printerr ("\n");
}

int
main (int argc, char **argv)
{
    GOptionContext *context;
    GError *error = NULL;
    char *protocol_str = NULL;
    char *gateway = NULL;
    char *username = NULL;
    char *cookie = NULL;
    char *usergroup = NULL;
    char *extra_args = NULL;
    OcRunnerProtocol protocol;

    GOptionEntry entries[] = {
        { "protocol", 'p', 0, G_OPTION_ARG_STRING, &protocol_str,
          "VPN protocol (gp or anyconnect)", "PROTOCOL" },
        { "gateway", 'g', 0, G_OPTION_ARG_STRING, &gateway,
          "VPN gateway hostname", "HOSTNAME" },
        { "username", 'u', 0, G_OPTION_ARG_STRING, &username,
          "Username (optional)", "USERNAME" },
        { "cookie", 'c', 0, G_OPTION_ARG_STRING, &cookie,
          "SSO authentication cookie", "COOKIE" },
        { "usergroup", 'G', 0, G_OPTION_ARG_STRING, &usergroup,
          "User group for GlobalProtect (default: portal:prelogin-cookie)", "USERGROUP" },
        { "extra-args", 'e', 0, G_OPTION_ARG_STRING, &extra_args,
          "Extra openconnect arguments", "ARGS" },
        { NULL }
    };

    /* Parse command line */
    context = g_option_context_new ("- OpenConnect Runner Example");
    g_option_context_add_main_entries (context, entries, NULL);
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_printerr ("Option parsing failed: %s\n", error->message);
        g_error_free (error);
        g_option_context_free (context);
        return 1;
    }
    g_option_context_free (context);

    /* Validate arguments */
    if (!protocol_str || !gateway || !cookie) {
        g_printerr ("Error: --protocol, --gateway, and --cookie are required\n\n");
        g_printerr ("Example:\n");
        g_printerr ("  %s --protocol=gp --gateway=vpn.example.com \\\n", argv[0]);
        g_printerr ("     --username=user@example.com --cookie=\"<YOUR_COOKIE>\"\n\n");
        return 1;
    }

    /* Parse protocol */
    if (g_strcmp0 (protocol_str, "gp") == 0 ||
        g_strcmp0 (protocol_str, "globalprotect") == 0) {
        protocol = OC_RUNNER_PROTOCOL_GLOBALPROTECT;
    } else if (g_strcmp0 (protocol_str, "anyconnect") == 0 ||
               g_strcmp0 (protocol_str, "ac") == 0) {
        protocol = OC_RUNNER_PROTOCOL_ANYCONNECT;
    } else {
        g_printerr ("Error: Unknown protocol '%s' (use 'gp' or 'anyconnect')\n", protocol_str);
        return 1;
    }

    /* Set up signal handlers */
    signal (SIGINT, signal_handler);
    signal (SIGTERM, signal_handler);

    /* Create main loop */
    main_loop = g_main_loop_new (NULL, FALSE);

    /* Create runner */
    runner = oc_runner_new ();

    /* Connect signals */
    g_signal_connect (runner, "state-changed",
                     G_CALLBACK (on_state_changed), GINT_TO_POINTER (1));
    g_signal_connect (runner, "tunnel-ready",
                     G_CALLBACK (on_tunnel_ready), NULL);
    g_signal_connect (runner, "log-message",
                     G_CALLBACK (on_log_message), NULL);
    g_signal_connect (runner, "error-occurred",
                     G_CALLBACK (on_error_occurred), NULL);

    /* Print banner */
    g_print ("\n");
    g_print ("╔════════════════════════════════════════════╗\n");
    g_print ("║   OpenConnect Runner Example               ║\n");
    g_print ("╚════════════════════════════════════════════╝\n");
    g_print ("\n");
    g_print ("  Protocol:  %s\n", protocol_str);
    g_print ("  Gateway:   %s\n", gateway);
    if (username)
        g_print ("  Username:  %s\n", username);
    if (usergroup)
        g_print ("  Usergroup: %s\n", usergroup);
    if (extra_args)
        g_print ("  Extra:     %s\n", extra_args);
    g_print ("  Cookie:    %.*s... (length: %zu)\n",
             (int)MIN(20, strlen(cookie)), cookie, strlen(cookie));
    g_print ("\n");
    g_print ("Connecting...\n");
    g_print ("\n");

    /* Start connection */
    if (!oc_runner_connect (runner,
                           protocol,
                           gateway,
                           username,
                           cookie,
                           usergroup,
                           extra_args,
                           &error)) {
        g_printerr ("Failed to start connection: %s\n", error->message);
        g_error_free (error);
        g_object_unref (runner);
        g_main_loop_unref (main_loop);
        return 1;
    }

    /* Run main loop */
    g_main_loop_run (main_loop);

    /* Cleanup */
    g_print ("\nCleaning up...\n");
    oc_runner_disconnect (runner);
    g_object_unref (runner);
    g_main_loop_unref (main_loop);

    g_free (protocol_str);
    g_free (gateway);
    g_free (username);
    g_free (cookie);
    g_free (usergroup);
    g_free (extra_args);

    g_print ("Done.\n\n");
    return 0;
}
