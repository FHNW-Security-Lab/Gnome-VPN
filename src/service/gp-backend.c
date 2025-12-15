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
#include "gp-backend.h"
#include "utils.h"

#include <string.h>
#include <glib.h>
#include <gio/gio.h>

/**
 * SECTION:gp-backend
 * @title: GlobalProtect Backend
 * @short_description: GlobalProtect SSO authentication backend
 *
 * The GlobalProtect backend wraps gp-saml-gui to perform SAML-based
 * SSO authentication for Palo Alto GlobalProtect VPNs.
 *
 * The authentication flow:
 * 1. Spawn gp-saml-gui with the portal address
 * 2. User completes SAML authentication in the browser
 * 3. gp-saml-gui outputs the prelogin cookie
 * 4. Parse and return the cookie for use with openconnect
 */

#define GP_SAML_GUI_TIMEOUT_SECONDS (300)  /* 5 minutes */

typedef struct {
    gchar *gateway;
    gchar *username;
    GSubprocess *subprocess;
    GCancellable *cancellable;
    gulong cancel_id;
} GpAuthData;

static void
gp_auth_data_free (GpAuthData *data)
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
on_gp_subprocess_cancelled (GCancellable *cancellable,
                            gpointer      user_data)
{
    GpAuthData *data = user_data;

    g_debug ("GlobalProtect authentication cancelled");

    if (data->subprocess) {
        /* Terminate the subprocess */
        g_subprocess_force_exit (data->subprocess);
    }
}

static gchar *
extract_prelogin_cookie (const gchar *output)
{
    g_auto(GStrv) lines = NULL;
    gchar *cookie = NULL;

    if (!output || !*output)
        return NULL;

    lines = g_strsplit (output, "\n", -1);

    /* Look for prelogin-cookie in the output
     * gp-saml-gui outputs it in the format:
     * prelogin-cookie=<cookie_value>
     * or just the cookie value alone on stdout
     */
    for (gsize i = 0; lines[i]; i++) {
        gchar *line = g_strstrip (lines[i]);

        if (!*line)
            continue;

        /* Check for "prelogin-cookie=" prefix */
        if (g_str_has_prefix (line, "prelogin-cookie=")) {
            cookie = g_strdup (line + strlen ("prelogin-cookie="));
            break;
        }
        /* Check for standalone cookie (non-empty, non-error line) */
        else if (!g_str_has_prefix (line, "ERROR") &&
                 !g_str_has_prefix (line, "WARNING") &&
                 strlen (line) > 20) {  /* Cookies are typically long */
            cookie = g_strdup (line);
            break;
        }
    }

    return cookie;
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
     * gp-saml-gui may output: user=<username>
     */
    for (gsize i = 0; lines[i]; i++) {
        gchar *line = g_strstrip (lines[i]);

        if (g_str_has_prefix (line, "user=")) {
            username = g_strdup (line + strlen ("user="));
            break;
        }
    }

    return username;
}

static void
gp_subprocess_communicate_cb (GObject      *source,
                              GAsyncResult *result,
                              gpointer      user_data)
{
    GSubprocess *subprocess = G_SUBPROCESS (source);
    GTask *task = G_TASK (user_data);
    GpAuthData *data = g_task_get_task_data (task);
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

    /* Check if the subprocess was successful */
    if (!g_subprocess_get_successful (subprocess)) {
        gint exit_status = g_subprocess_get_exit_status (subprocess);

        g_debug ("gp-saml-gui failed with exit status %d", exit_status);
        g_debug ("stderr: %s", stderr_buf ? stderr_buf : "(empty)");

        g_task_return_new_error (task,
                                G_IO_ERROR,
                                G_IO_ERROR_FAILED,
                                "gp-saml-gui failed (exit %d): %s",
                                exit_status,
                                stderr_buf ? stderr_buf : "unknown error");
        g_object_unref (task);
        return;
    }

    g_debug ("gp-saml-gui completed successfully");
    g_debug ("stdout: %s", stdout_buf ? stdout_buf : "(empty)");

    /* Extract the prelogin cookie from output */
    credentials = vpn_sso_credentials_new ();
    credentials->protocol = VPN_SSO_PROTOCOL_GLOBALPROTECT;
    credentials->gateway = g_strdup (data->gateway);
    credentials->cookie = extract_prelogin_cookie (stdout_buf);

    /* Try to extract username if not provided */
    if (data->username)
        credentials->username = g_strdup (data->username);
    else
        credentials->username = extract_username_from_output (stdout_buf);

    /* Set usergroup for GlobalProtect */
    credentials->usergroup = g_strdup ("portal:prelogin-cookie");

    if (credentials->cookie && *credentials->cookie) {
        credentials->success = TRUE;
        g_debug ("Successfully obtained prelogin cookie");
        g_task_return_pointer (task, credentials, (GDestroyNotify) vpn_sso_credentials_free);
    } else {
        credentials->success = FALSE;
        credentials->error_message = g_strdup ("Failed to extract prelogin cookie from gp-saml-gui output");
        g_task_return_new_error (task,
                                G_IO_ERROR,
                                G_IO_ERROR_FAILED,
                                "Failed to extract prelogin cookie");
        vpn_sso_credentials_free (credentials);
    }

    g_object_unref (task);
}

/**
 * vpn_sso_gp_authenticate_async:
 *
 * Initiates GlobalProtect SSO authentication.
 */
void
vpn_sso_gp_authenticate_async (const gchar         *gateway,
                               const gchar         *username,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
    GTask *task;
    GpAuthData *data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocessLauncher) launcher = NULL;
    const gchar *argv[10];
    gint argc = 0;

    g_return_if_fail (gateway != NULL);

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_gp_authenticate_async);

    /* Create auth data */
    data = g_new0 (GpAuthData, 1);
    data->gateway = g_strdup (gateway);
    data->username = g_strdup (username);
    data->cancellable = cancellable ? g_object_ref (cancellable) : NULL;

    g_task_set_task_data (task, data, (GDestroyNotify) gp_auth_data_free);

    /* Set up cancellation */
    if (cancellable) {
        data->cancel_id = g_cancellable_connect (cancellable,
                                                 G_CALLBACK (on_gp_subprocess_cancelled),
                                                 data,
                                                 NULL);
    }

    /* Build command line for gp-saml-gui */
    argv[argc++] = "gp-saml-gui";
    argv[argc++] = "--portal";
    argv[argc++] = gateway;
    /* Pass username to pre-fill in SSO browser if available */
    if (username && *username) {
        argv[argc++] = "--user";
        argv[argc++] = username;
    }
    argv[argc++] = "--";
    argv[argc++] = "--protocol=gp";
    argv[argc] = NULL;

    if (username && *username)
        g_debug ("Spawning: %s --portal %s --user %s -- --protocol=gp", argv[0], gateway, username);
    else
        g_debug ("Spawning: %s --portal %s -- --protocol=gp", argv[0], gateway);

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
                                        gp_subprocess_communicate_cb,
                                        task);
}

/**
 * vpn_sso_gp_authenticate_finish:
 *
 * Completes a GlobalProtect authentication operation.
 */
VpnSsoCredentials *
vpn_sso_gp_authenticate_finish (GAsyncResult  *result,
                                GError       **error)
{
    g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
}
