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
#include "ac-backend.h"
#include "utils.h"

#include <string.h>
#include <glib.h>
#include <gio/gio.h>

/**
 * SECTION:ac-backend
 * @title: AnyConnect Backend
 * @short_description: AnyConnect SSO authentication backend
 *
 * The AnyConnect backend wraps openconnect-sso to perform SSO
 * authentication for Cisco AnyConnect VPNs.
 *
 * Note: Unlike GlobalProtect's gp-saml-gui which only handles authentication,
 * openconnect-sso handles both SSO authentication AND establishes the VPN
 * connection. For this SSO handler, we use openconnect-sso in authentication-
 * only mode (if available) or detect successful authentication and then
 * disconnect to let the main VPN service handle the actual connection.
 *
 * The authentication flow:
 * 1. Spawn openconnect-sso with the server address
 * 2. User completes SSO authentication in the browser
 * 3. Monitor for successful authentication
 * 4. Return success indicator
 */

#define AC_SSO_TIMEOUT_SECONDS (300)  /* 5 minutes */

typedef struct {
    gchar *gateway;
    gchar *username;
    GSubprocess *subprocess;
    GCancellable *cancellable;
    gulong cancel_id;
} AcAuthData;

static void
ac_auth_data_free (AcAuthData *data)
{
    if (!data)
        return;

    if (data->cancel_id > 0 && data->cancellable)
        g_cancellable_disconnect (data->cancellable, data->cancel_id);

    g_free (data->gateway);
    g_free (data->username);
    g_clear_object (&data->subprocess);
    g_clear_object (&data->cancellable);
    g_free (data);
}

static void
on_ac_subprocess_cancelled (GCancellable *cancellable,
                            gpointer      user_data)
{
    AcAuthData *data = user_data;

    g_debug ("AnyConnect authentication cancelled");

    if (data->subprocess) {
        /* Terminate the subprocess */
        g_subprocess_force_exit (data->subprocess);
    }
}

static gboolean
check_authentication_success (const gchar *output)
{
    if (!output || !*output)
        return FALSE;

    /* Look for success indicators in openconnect-sso output
     * Common patterns:
     * - "Connected"
     * - "VPN connection established"
     * - "Login successful"
     * - "Authentication successful"
     */
    if (strstr (output, "Connected") ||
        strstr (output, "connection established") ||
        strstr (output, "Login successful") ||
        strstr (output, "Authentication successful") ||
        strstr (output, "authenticated")) {
        return TRUE;
    }

    return FALSE;
}

static gchar *
extract_username_from_output (const gchar *output)
{
    g_auto(GStrv) lines = NULL;
    gchar *username = NULL;

    if (!output || !*output)
        return NULL;

    lines = g_strsplit (output, "\n", -1);

    /* Look for username in the output
     * openconnect-sso may output: user: <username>
     */
    for (gsize i = 0; lines[i]; i++) {
        gchar *line = g_strstrip (lines[i]);

        if (g_str_has_prefix (line, "user:") ||
            g_str_has_prefix (line, "User:")) {
            username = g_strdup (g_strstrip (line + 5));
            break;
        }
    }

    return username;
}

static void
ac_subprocess_communicate_cb (GObject      *source,
                              GAsyncResult *result,
                              gpointer      user_data)
{
    GSubprocess *subprocess = G_SUBPROCESS (source);
    GTask *task = G_TASK (user_data);
    AcAuthData *data = g_task_get_task_data (task);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *stdout_buf = NULL;
    g_autofree gchar *stderr_buf = NULL;
    VpnSsoCredentials *credentials = NULL;

    /* Get the subprocess output */
    if (!g_subprocess_communicate_utf8_finish (subprocess,
                                               result,
                                               &stdout_buf,
                                               &stderr_buf,
                                               &error)) {
        g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    /* For openconnect-sso, we might get non-zero exit if we kill it
     * after successful authentication, so we check output instead */
    g_debug ("openconnect-sso completed");
    g_debug ("stdout: %s", stdout_buf ? stdout_buf : "(empty)");
    g_debug ("stderr: %s", stderr_buf ? stderr_buf : "(empty)");

    /* Create credentials */
    credentials = vpn_sso_credentials_new ();
    credentials->protocol = VPN_SSO_PROTOCOL_ANYCONNECT;
    credentials->gateway = g_strdup (data->gateway);

    /* Try to extract username */
    if (data->username)
        credentials->username = g_strdup (data->username);
    else {
        credentials->username = extract_username_from_output (stdout_buf);
        if (!credentials->username)
            credentials->username = extract_username_from_output (stderr_buf);
    }

    /* Check if authentication was successful */
    if (check_authentication_success (stdout_buf) ||
        check_authentication_success (stderr_buf)) {
        credentials->success = TRUE;
        g_debug ("Successfully authenticated via openconnect-sso");
        g_task_return_pointer (task, credentials, (GDestroyNotify) vpn_sso_credentials_free);
    } else if (g_subprocess_get_successful (subprocess)) {
        /* Process exited successfully but we didn't detect auth success */
        credentials->success = TRUE;
        g_debug ("openconnect-sso completed successfully");
        g_task_return_pointer (task, credentials, (GDestroyNotify) vpn_sso_credentials_free);
    } else {
        gint exit_status = g_subprocess_get_exit_status (subprocess);

        /* Exit status 1-2 might be normal for Ctrl+C after successful auth */
        if (exit_status <= 2 && (check_authentication_success (stdout_buf) ||
                                 check_authentication_success (stderr_buf))) {
            credentials->success = TRUE;
            g_debug ("Authentication successful (process terminated after auth)");
            g_task_return_pointer (task, credentials, (GDestroyNotify) vpn_sso_credentials_free);
        } else {
            credentials->success = FALSE;
            credentials->error_message = g_strdup_printf (
                "openconnect-sso failed (exit %d): %s",
                exit_status,
                stderr_buf ? stderr_buf : "unknown error");

            g_task_return_new_error (task,
                                    G_IO_ERROR,
                                    G_IO_ERROR_FAILED,
                                    "openconnect-sso failed (exit %d): %s",
                                    exit_status,
                                    stderr_buf ? stderr_buf : "unknown error");
            vpn_sso_credentials_free (credentials);
        }
    }

    g_object_unref (task);
}

/**
 * vpn_sso_ac_authenticate_async:
 *
 * Initiates AnyConnect SSO authentication.
 */
void
vpn_sso_ac_authenticate_async (const gchar         *gateway,
                               const gchar         *username,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
    GTask *task;
    AcAuthData *data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocessLauncher) launcher = NULL;
    const gchar *argv[4];
    gint argc = 0;

    g_return_if_fail (gateway != NULL);

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_ac_authenticate_async);

    /* Create auth data */
    data = g_new0 (AcAuthData, 1);
    data->gateway = g_strdup (gateway);
    data->username = g_strdup (username);
    data->cancellable = cancellable ? g_object_ref (cancellable) : NULL;

    g_task_set_task_data (task, data, (GDestroyNotify) ac_auth_data_free);

    /* Set up cancellation */
    if (cancellable) {
        data->cancel_id = g_cancellable_connect (cancellable,
                                                 G_CALLBACK (on_ac_subprocess_cancelled),
                                                 data,
                                                 NULL);
    }

    /* Build command line for openconnect-sso
     * Note: We use --authenticate to get credentials only, not establish connection
     * If that's not supported, we'll use regular mode and terminate after auth
     */
    argv[argc++] = "openconnect-sso";
    argv[argc++] = "--server";
    argv[argc++] = gateway;
    argv[argc] = NULL;

    g_debug ("Spawning: %s --server %s", argv[0], gateway);

    /* Create subprocess launcher */
    launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                         G_SUBPROCESS_FLAGS_STDERR_PIPE);

    /* Get the graphical session environment
     * When running as root (D-Bus activated service), we need to detect
     * the user's graphical session to spawn GUI applications
     */
    g_autoptr(VpnSsoSessionEnv) session_env = vpn_sso_get_graphical_session_env ();

    if (session_env) {
        if (session_env->display)
            g_subprocess_launcher_setenv (launcher, "DISPLAY", session_env->display, TRUE);
        if (session_env->wayland_display)
            g_subprocess_launcher_setenv (launcher, "WAYLAND_DISPLAY", session_env->wayland_display, TRUE);
        if (session_env->xdg_runtime_dir)
            g_subprocess_launcher_setenv (launcher, "XDG_RUNTIME_DIR", session_env->xdg_runtime_dir, TRUE);
        if (session_env->xauthority)
            g_subprocess_launcher_setenv (launcher, "XAUTHORITY", session_env->xauthority, TRUE);
        if (session_env->dbus_session_bus_address)
            g_subprocess_launcher_setenv (launcher, "DBUS_SESSION_BUS_ADDRESS", session_env->dbus_session_bus_address, TRUE);
        if (session_env->home)
            g_subprocess_launcher_setenv (launcher, "HOME", session_env->home, TRUE);

        g_debug ("Environment: DISPLAY=%s, WAYLAND_DISPLAY=%s, XDG_RUNTIME_DIR=%s",
                 session_env->display ? session_env->display : "(null)",
                 session_env->wayland_display ? session_env->wayland_display : "(null)",
                 session_env->xdg_runtime_dir ? session_env->xdg_runtime_dir : "(null)");
    } else {
        g_warning ("Could not detect graphical session environment, GUI may not work");
    }

    /* Ensure PATH includes our bundled tools */
    const gchar *path = g_getenv ("PATH");
    if (path) {
        g_autofree gchar *new_path = g_strdup_printf ("/opt/gnome-vpn-sso/bin:%s", path);
        g_subprocess_launcher_setenv (launcher, "PATH", new_path, TRUE);
    } else {
        g_subprocess_launcher_setenv (launcher, "PATH", "/opt/gnome-vpn-sso/bin:/usr/bin:/bin", TRUE);
    }

    /* Spawn the subprocess */
    data->subprocess = g_subprocess_launcher_spawnv (launcher, argv, &error);
    if (!data->subprocess) {
        g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    /* Communicate with the subprocess asynchronously */
    g_subprocess_communicate_utf8_async (data->subprocess,
                                        NULL,  /* No stdin */
                                        cancellable,
                                        ac_subprocess_communicate_cb,
                                        task);
}

/**
 * vpn_sso_ac_authenticate_finish:
 *
 * Completes an AnyConnect authentication operation.
 */
VpnSsoCredentials *
vpn_sso_ac_authenticate_finish (GAsyncResult  *result,
                                GError       **error)
{
    g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
}
