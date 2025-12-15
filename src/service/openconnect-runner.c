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
#include "openconnect-runner.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

/**
 * SECTION:openconnect-runner
 * @title: OcRunner
 * @short_description: OpenConnect process manager
 *
 * #OcRunner manages the lifecycle of an OpenConnect VPN connection.
 * It spawns the openconnect process, monitors its output, and reports
 * connection status and tunnel configuration.
 */

struct _OcRunnerPrivate {
    /* Connection parameters */
    OcRunnerProtocol protocol;
    char *gateway;
    char *username;
    char *cookie;
    char *usergroup;
    char *extra_args;

    /* Process management */
    GSubprocess *subprocess;
    GCancellable *cancellable;

    /* State */
    OcRunnerState state;
    char *tunnel_ip4;
    char *tunnel_ip6;
    GHashTable *config;

    /* Output monitoring */
    GDataInputStream *stdout_stream;
    GDataInputStream *stderr_stream;
    guint stdout_watch_id;
    guint stderr_watch_id;

    /* Disconnect handling */
    guint disconnect_timeout_id;
};

enum {
    SIGNAL_STATE_CHANGED,
    SIGNAL_TUNNEL_READY,
    SIGNAL_LOG_MESSAGE,
    SIGNAL_ERROR_OCCURRED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (OcRunner, oc_runner, G_TYPE_OBJECT)

/* Forward declarations */
static void oc_runner_set_state (OcRunner *runner, OcRunnerState state);
static void oc_runner_cleanup_process (OcRunner *runner);
static void oc_runner_parse_output_line (OcRunner *runner, const char *line, gboolean is_stderr);

static void
oc_runner_init (OcRunner *runner)
{
    runner->priv = oc_runner_get_instance_private (runner);
    runner->priv->state = OC_RUNNER_STATE_IDLE;
    runner->priv->config = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
oc_runner_finalize (GObject *object)
{
    OcRunner *runner = OC_RUNNER (object);
    OcRunnerPrivate *priv = runner->priv;

    oc_runner_disconnect (runner);

    g_clear_pointer (&priv->gateway, g_free);
    g_clear_pointer (&priv->username, g_free);
    g_clear_pointer (&priv->cookie, g_free);
    g_clear_pointer (&priv->usergroup, g_free);
    g_clear_pointer (&priv->extra_args, g_free);
    g_clear_pointer (&priv->tunnel_ip4, g_free);
    g_clear_pointer (&priv->tunnel_ip6, g_free);
    g_clear_pointer (&priv->config, g_hash_table_unref);

    G_OBJECT_CLASS (oc_runner_parent_class)->finalize (object);
}

static void
oc_runner_class_init (OcRunnerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = oc_runner_finalize;

    /**
     * OcRunner::state-changed:
     * @runner: the #OcRunner
     * @state: the new connection state
     *
     * Emitted when the connection state changes.
     */
    signals[SIGNAL_STATE_CHANGED] =
        g_signal_new ("state-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (OcRunnerClass, state_changed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * OcRunner::tunnel-ready:
     * @runner: the #OcRunner
     * @ip4_address: IPv4 address assigned (or %NULL)
     * @ip6_address: IPv6 address assigned (or %NULL)
     * @config: tunnel configuration parameters
     *
     * Emitted when the VPN tunnel is established and ready.
     */
    signals[SIGNAL_TUNNEL_READY] =
        g_signal_new ("tunnel-ready",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (OcRunnerClass, tunnel_ready),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_HASH_TABLE);

    /**
     * OcRunner::log-message:
     * @runner: the #OcRunner
     * @message: the log message
     *
     * Emitted for informational log messages from OpenConnect.
     */
    signals[SIGNAL_LOG_MESSAGE] =
        g_signal_new ("log-message",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (OcRunnerClass, log_message),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * OcRunner::error-occurred:
     * @runner: the #OcRunner
     * @error: the error that occurred
     *
     * Emitted when an error occurs during connection.
     */
    signals[SIGNAL_ERROR_OCCURRED] =
        g_signal_new ("error-occurred",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (OcRunnerClass, error_occurred),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_ERROR);
}

/**
 * oc_runner_new:
 *
 * Creates a new #OcRunner instance.
 *
 * Returns: (transfer full): a new #OcRunner
 */
OcRunner *
oc_runner_new (void)
{
    return g_object_new (OC_TYPE_RUNNER, NULL);
}

static void
oc_runner_set_state (OcRunner *runner, OcRunnerState state)
{
    OcRunnerPrivate *priv = runner->priv;

    if (priv->state != state) {
        priv->state = state;
        g_signal_emit (runner, signals[SIGNAL_STATE_CHANGED], 0, state);
        g_debug ("OpenConnect state changed to: %s", oc_runner_state_to_string (state));
    }
}

static void
oc_runner_emit_log (OcRunner *runner, const char *message)
{
    g_signal_emit (runner, signals[SIGNAL_LOG_MESSAGE], 0, message);
}

static void
oc_runner_emit_error (OcRunner *runner, GError *error)
{
    g_signal_emit (runner, signals[SIGNAL_ERROR_OCCURRED], 0, error);
    oc_runner_set_state (runner, OC_RUNNER_STATE_FAILED);
}

static void
oc_runner_parse_output_line (OcRunner *runner, const char *line, gboolean is_stderr)
{
    OcRunnerPrivate *priv = runner->priv;

    if (!line || !*line)
        return;

    g_debug ("OpenConnect %s: %s", is_stderr ? "stderr" : "stdout", line);

    /* Parse common status messages */
    if (g_str_has_prefix (line, "Connected")) {
        oc_runner_set_state (runner, OC_RUNNER_STATE_CONNECTED);
    } else if (strstr (line, "RTNETLINK answers: File exists") != NULL) {
        /* Ignore route exists errors - these are usually harmless */
        return;
    } else if (g_str_has_prefix (line, "Established") ||
               strstr (line, "tunnel connected") != NULL) {
        oc_runner_set_state (runner, OC_RUNNER_STATE_CONNECTED);
    } else if (strstr (line, "DTLS handshake") != NULL) {
        oc_runner_set_state (runner, OC_RUNNER_STATE_CONNECTING);
    } else if (strstr (line, "SSL connected") != NULL) {
        oc_runner_set_state (runner, OC_RUNNER_STATE_CONNECTING);
    } else if (g_str_has_prefix (line, "Got CONNECT response:")) {
        oc_runner_set_state (runner, OC_RUNNER_STATE_AUTHENTICATING);
    }

    /* Parse IP configuration */
    if (strstr (line, "Connected as") != NULL) {
        /* Format: "Connected as 192.168.1.100, using SSL + LZ4" */
        char **parts = g_strsplit (line, " ", -1);
        for (int i = 0; parts[i] != NULL; i++) {
            if (g_str_has_suffix (parts[i], ",") || g_str_has_suffix (parts[i], ";")) {
                char *ip = g_strndup (parts[i], strlen (parts[i]) - 1);
                if (g_hostname_is_ip_address (ip)) {
                    if (strchr (ip, ':')) {
                        g_free (priv->tunnel_ip6);
                        priv->tunnel_ip6 = g_strdup (ip);
                    } else {
                        g_free (priv->tunnel_ip4);
                        priv->tunnel_ip4 = g_strdup (ip);
                    }
                }
                g_free (ip);
            }
        }
        g_strfreev (parts);
    }

    /* Parse tunnel device name */
    if (strstr (line, "tun") != NULL || strstr (line, "utun") != NULL) {
        char *dev_start = strstr (line, "tun");
        if (!dev_start)
            dev_start = strstr (line, "utun");

        if (dev_start) {
            char device[32] = {0};
            sscanf (dev_start, "%31s", device);
            g_hash_table_insert (priv->config,
                                g_strdup ("tunnel-device"),
                                g_strdup (device));
        }
    }

    /* Parse DNS servers */
    if (strstr (line, "DNS") != NULL && strstr (line, "server") != NULL) {
        /* Try to extract IP addresses from DNS lines */
        const char *ip_start = line;
        while ((ip_start = strchr (ip_start, ' ')) != NULL) {
            ip_start++;
            if (g_ascii_isdigit (*ip_start)) {
                char ip[64];
                if (sscanf (ip_start, "%63s", ip) == 1) {
                    if (g_hostname_is_ip_address (ip)) {
                        g_hash_table_insert (priv->config,
                                           g_strdup_printf ("dns-server-%d",
                                                          g_hash_table_size (priv->config)),
                                           g_strdup (ip));
                    }
                }
            }
        }
    }

    /* Parse split routing information */
    if (strstr (line, "Split") != NULL && strstr (line, "route") != NULL) {
        /* Format: "Split Include route: 10.0.0.0/8" */
        char *route_start = strstr (line, ": ");
        if (route_start) {
            route_start += 2;
            char route[128];
            if (sscanf (route_start, "%127s", route) == 1) {
                g_hash_table_insert (priv->config,
                                   g_strdup_printf ("route-%d",
                                                  g_hash_table_size (priv->config)),
                                   g_strdup (route));
            }
        }
    }

    /* Detect errors */
    if (strstr (line, "Failed") != NULL ||
        strstr (line, "ERROR") != NULL ||
        strstr (line, "error:") != NULL ||
        (strstr (line, "Cookie") != NULL && strstr (line, "rejected") != NULL)) {
        GError *error = g_error_new_literal (G_IO_ERROR,
                                            G_IO_ERROR_FAILED,
                                            line);
        oc_runner_emit_error (runner, error);
        g_error_free (error);
        return;
    }

    /* Emit as log message */
    oc_runner_emit_log (runner, line);

    /* Check if we should emit tunnel-ready */
    if (priv->state == OC_RUNNER_STATE_CONNECTED &&
        (priv->tunnel_ip4 || priv->tunnel_ip6)) {
        static gboolean emitted = FALSE;
        if (!emitted) {
            emitted = TRUE;
            g_signal_emit (runner, signals[SIGNAL_TUNNEL_READY], 0,
                          priv->tunnel_ip4, priv->tunnel_ip6, priv->config);
        }
    }
}

static void
oc_runner_read_output_cb (GObject *source_object,
                          GAsyncResult *res,
                          gpointer user_data)
{
    OcRunner *runner = OC_RUNNER (user_data);
    GDataInputStream *stream = G_DATA_INPUT_STREAM (source_object);
    GError *error = NULL;
    char *line;
    gboolean is_stderr = (stream == runner->priv->stderr_stream);

    line = g_data_input_stream_read_line_finish (stream, res, NULL, &error);

    if (error) {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_debug ("Error reading OpenConnect output: %s", error->message);
        }
        g_error_free (error);
        return;
    }

    if (line) {
        oc_runner_parse_output_line (runner, line, is_stderr);
        g_free (line);

        /* Continue reading */
        g_data_input_stream_read_line_async (stream,
                                            G_PRIORITY_DEFAULT,
                                            runner->priv->cancellable,
                                            oc_runner_read_output_cb,
                                            runner);
        return;
    }

    /* EOF reached */
    return;
}

static void
oc_runner_start_output_monitoring (OcRunner *runner)
{
    OcRunnerPrivate *priv = runner->priv;
    GInputStream *stdout_stream, *stderr_stream;

    stdout_stream = g_subprocess_get_stdout_pipe (priv->subprocess);
    stderr_stream = g_subprocess_get_stderr_pipe (priv->subprocess);

    if (stdout_stream) {
        priv->stdout_stream = g_data_input_stream_new (stdout_stream);
        g_data_input_stream_read_line_async (priv->stdout_stream,
                                            G_PRIORITY_DEFAULT,
                                            priv->cancellable,
                                            oc_runner_read_output_cb,
                                            runner);
    }

    if (stderr_stream) {
        priv->stderr_stream = g_data_input_stream_new (stderr_stream);
        g_data_input_stream_read_line_async (priv->stderr_stream,
                                            G_PRIORITY_DEFAULT,
                                            priv->cancellable,
                                            oc_runner_read_output_cb,
                                            runner);
    }
}

static void
oc_runner_wait_check_cb (GObject *source_object,
                         GAsyncResult *res,
                         gpointer user_data)
{
    OcRunner *runner = OC_RUNNER (user_data);
    GSubprocess *subprocess = G_SUBPROCESS (source_object);
    GError *error = NULL;

    g_subprocess_wait_check_finish (subprocess, res, &error);

    if (error) {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_debug ("OpenConnect process exited with error: %s", error->message);
            oc_runner_emit_error (runner, error);
        }
        g_error_free (error);
    }

    if (runner->priv->state != OC_RUNNER_STATE_DISCONNECTING &&
        runner->priv->state != OC_RUNNER_STATE_IDLE) {
        oc_runner_set_state (runner, OC_RUNNER_STATE_FAILED);
    }

    oc_runner_cleanup_process (runner);
}

static void
oc_runner_cleanup_process (OcRunner *runner)
{
    OcRunnerPrivate *priv = runner->priv;

    if (priv->disconnect_timeout_id > 0) {
        g_source_remove (priv->disconnect_timeout_id);
        priv->disconnect_timeout_id = 0;
    }

    if (priv->cancellable) {
        g_cancellable_cancel (priv->cancellable);
        g_clear_object (&priv->cancellable);
    }

    g_clear_object (&priv->stdout_stream);
    g_clear_object (&priv->stderr_stream);
    g_clear_object (&priv->subprocess);

    if (priv->state == OC_RUNNER_STATE_DISCONNECTING) {
        oc_runner_set_state (runner, OC_RUNNER_STATE_IDLE);
    }
}

static gboolean
oc_runner_force_kill_timeout (gpointer user_data)
{
    OcRunner *runner = OC_RUNNER (user_data);
    OcRunnerPrivate *priv = runner->priv;

    g_warning ("OpenConnect process did not terminate gracefully, forcing kill");

    if (priv->subprocess) {
        g_subprocess_force_exit (priv->subprocess);
    }

    priv->disconnect_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

/**
 * oc_runner_connect:
 * @runner: a #OcRunner
 * @protocol: VPN protocol to use
 * @gateway: VPN gateway address
 * @username: username for authentication
 * @cookie: SSO authentication cookie
 * @usergroup: user group (for GlobalProtect)
 * @extra_args: additional openconnect arguments (or %NULL)
 * @error: return location for error
 *
 * Starts an OpenConnect VPN connection.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
oc_runner_connect (OcRunner          *runner,
                   OcRunnerProtocol   protocol,
                   const char        *gateway,
                   const char        *username,
                   const char        *cookie,
                   const char        *usergroup,
                   const char        *extra_args,
                   GError           **error)
{
    OcRunnerPrivate *priv;
    GSubprocessLauncher *launcher;
    GPtrArray *argv;
    GOutputStream *stdin_pipe;
    GError *local_error = NULL;
    const char *protocol_name;

    g_return_val_if_fail (OC_IS_RUNNER (runner), FALSE);
    g_return_val_if_fail (gateway != NULL, FALSE);
    g_return_val_if_fail (cookie != NULL, FALSE);

    priv = runner->priv;

    if (priv->subprocess) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_BUSY,
                           "Connection already in progress");
        return FALSE;
    }

    /* Store connection parameters */
    priv->protocol = protocol;
    g_free (priv->gateway);
    priv->gateway = g_strdup (gateway);
    g_free (priv->username);
    priv->username = g_strdup (username);
    g_free (priv->cookie);
    priv->cookie = g_strdup (cookie);
    g_free (priv->usergroup);
    priv->usergroup = g_strdup (usergroup);
    g_free (priv->extra_args);
    priv->extra_args = g_strdup (extra_args);

    /* Clear previous state */
    g_clear_pointer (&priv->tunnel_ip4, g_free);
    g_clear_pointer (&priv->tunnel_ip6, g_free);
    g_hash_table_remove_all (priv->config);

    /* Build command line */
    argv = g_ptr_array_new_with_free_func (g_free);

    /* Check if we need sudo/pkexec for root access */
    if (getuid () != 0) {
        g_ptr_array_add (argv, g_strdup ("pkexec"));
        g_ptr_array_add (argv, g_strdup ("--disable-internal-agent"));
    }

    g_ptr_array_add (argv, g_strdup ("openconnect"));

    /* Protocol-specific arguments */
    switch (protocol) {
        case OC_RUNNER_PROTOCOL_GLOBALPROTECT:
            protocol_name = "globalprotect";
            g_ptr_array_add (argv, g_strdup ("--protocol=gp"));
            g_ptr_array_add (argv, g_strdup ("--useragent=PAN GlobalProtect"));
            g_ptr_array_add (argv, g_strdup ("--os=linux-64"));
            if (usergroup && *usergroup) {
                g_ptr_array_add (argv, g_strdup_printf ("--usergroup=%s", usergroup));
            } else {
                g_ptr_array_add (argv, g_strdup ("--usergroup=portal:prelogin-cookie"));
            }
            break;

        case OC_RUNNER_PROTOCOL_ANYCONNECT:
            protocol_name = "anyconnect";
            g_ptr_array_add (argv, g_strdup ("--protocol=anyconnect"));
            break;

        default:
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       "Unknown protocol: %d", protocol);
            g_ptr_array_free (argv, TRUE);
            return FALSE;
    }

    /* Common arguments */
    if (username && *username) {
        g_ptr_array_add (argv, g_strdup_printf ("--user=%s", username));
    }

    g_ptr_array_add (argv, g_strdup ("--passwd-on-stdin"));
    g_ptr_array_add (argv, g_strdup ("--non-inter"));
    g_ptr_array_add (argv, g_strdup ("--reconnect-timeout=30"));

    /* Extra arguments */
    if (extra_args && *extra_args) {
        char **extra_argv = g_strsplit (extra_args, " ", -1);
        for (int i = 0; extra_argv[i] != NULL; i++) {
            if (*extra_argv[i]) {
                g_ptr_array_add (argv, g_strdup (extra_argv[i]));
            }
        }
        g_strfreev (extra_argv);
    }

    /* Gateway (must be last positional argument) */
    g_ptr_array_add (argv, g_strdup (gateway));
    g_ptr_array_add (argv, NULL);

    /* Debug: print command line */
    {
        char *cmdline = g_strjoinv (" ", (char **) argv->pdata);
        g_debug ("Starting OpenConnect: %s", cmdline);
        g_free (cmdline);
    }

    /* Create subprocess launcher */
    launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                         G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                         G_SUBPROCESS_FLAGS_STDERR_PIPE);

    /* Spawn process */
    priv->subprocess = g_subprocess_launcher_spawnv (launcher,
                                                     (const char * const *) argv->pdata,
                                                     &local_error);
    g_object_unref (launcher);
    g_ptr_array_free (argv, TRUE);

    if (!priv->subprocess) {
        g_propagate_error (error, local_error);
        return FALSE;
    }

    /* Write cookie to stdin */
    stdin_pipe = g_subprocess_get_stdin_pipe (priv->subprocess);

    if (!g_output_stream_write_all (stdin_pipe, cookie, strlen (cookie), NULL, NULL, &local_error) ||
        !g_output_stream_write_all (stdin_pipe, "\n", 1, NULL, NULL, &local_error) ||
        !g_output_stream_close (stdin_pipe, NULL, &local_error)) {
        g_subprocess_force_exit (priv->subprocess);
        g_object_unref (priv->subprocess);
        priv->subprocess = NULL;
        g_propagate_error (error, local_error);
        return FALSE;
    }

    /* Set up monitoring */
    priv->cancellable = g_cancellable_new ();
    oc_runner_start_output_monitoring (runner);

    /* Monitor process completion */
    g_subprocess_wait_check_async (priv->subprocess,
                                  priv->cancellable,
                                  oc_runner_wait_check_cb,
                                  runner);

    oc_runner_set_state (runner, OC_RUNNER_STATE_STARTING);

    g_debug ("OpenConnect process started for %s", protocol_name);
    return TRUE;
}

/**
 * oc_runner_disconnect:
 * @runner: a #OcRunner
 *
 * Disconnects an active VPN connection gracefully.
 */
void
oc_runner_disconnect (OcRunner *runner)
{
    OcRunnerPrivate *priv;

    g_return_if_fail (OC_IS_RUNNER (runner));

    priv = runner->priv;

    if (!priv->subprocess) {
        oc_runner_set_state (runner, OC_RUNNER_STATE_IDLE);
        return;
    }

    g_debug ("Disconnecting OpenConnect");

    oc_runner_set_state (runner, OC_RUNNER_STATE_DISCONNECTING);

    /* Try graceful termination first */
    g_subprocess_send_signal (priv->subprocess, SIGTERM);

    /* Set timeout for forced kill */
    priv->disconnect_timeout_id = g_timeout_add_seconds (5,
                                                         oc_runner_force_kill_timeout,
                                                         runner);
}

/**
 * oc_runner_get_state:
 * @runner: a #OcRunner
 *
 * Gets the current connection state.
 *
 * Returns: the current #OcRunnerState
 */
OcRunnerState
oc_runner_get_state (OcRunner *runner)
{
    g_return_val_if_fail (OC_IS_RUNNER (runner), OC_RUNNER_STATE_IDLE);
    return runner->priv->state;
}

/**
 * oc_runner_get_tunnel_ip4:
 * @runner: a #OcRunner
 *
 * Gets the IPv4 address assigned to the tunnel.
 *
 * Returns: (nullable): the IPv4 address or %NULL
 */
const char *
oc_runner_get_tunnel_ip4 (OcRunner *runner)
{
    g_return_val_if_fail (OC_IS_RUNNER (runner), NULL);
    return runner->priv->tunnel_ip4;
}

/**
 * oc_runner_get_tunnel_ip6:
 * @runner: a #OcRunner
 *
 * Gets the IPv6 address assigned to the tunnel.
 *
 * Returns: (nullable): the IPv6 address or %NULL
 */
const char *
oc_runner_get_tunnel_ip6 (OcRunner *runner)
{
    g_return_val_if_fail (OC_IS_RUNNER (runner), NULL);
    return runner->priv->tunnel_ip6;
}

/**
 * oc_runner_get_config:
 * @runner: a #OcRunner
 *
 * Gets the tunnel configuration parameters (DNS, routes, etc).
 *
 * Returns: (transfer none): configuration hash table
 */
GHashTable *
oc_runner_get_config (OcRunner *runner)
{
    g_return_val_if_fail (OC_IS_RUNNER (runner), NULL);
    return runner->priv->config;
}

/**
 * oc_runner_state_to_string:
 * @state: a #OcRunnerState
 *
 * Converts a state enum to a human-readable string.
 *
 * Returns: state name
 */
const char *
oc_runner_state_to_string (OcRunnerState state)
{
    switch (state) {
        case OC_RUNNER_STATE_IDLE:
            return "idle";
        case OC_RUNNER_STATE_STARTING:
            return "starting";
        case OC_RUNNER_STATE_AUTHENTICATING:
            return "authenticating";
        case OC_RUNNER_STATE_CONNECTING:
            return "connecting";
        case OC_RUNNER_STATE_CONNECTED:
            return "connected";
        case OC_RUNNER_STATE_DISCONNECTING:
            return "disconnecting";
        case OC_RUNNER_STATE_FAILED:
            return "failed";
        default:
            return "unknown";
    }
}
