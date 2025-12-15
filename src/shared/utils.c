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
#include "utils.h"

#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <glob.h>

const char *
vpn_sso_utils_get_version (void)
{
    return PACKAGE_VERSION;
}

void
vpn_sso_utils_init (void)
{
    /* Initialize utilities */
    g_debug ("VPN SSO utils initialized, version %s", PACKAGE_VERSION);
}

void
vpn_sso_utils_cleanup (void)
{
    /* Cleanup utilities */
    g_debug ("VPN SSO utils cleanup");
}

void
vpn_sso_session_env_free (VpnSsoSessionEnv *env)
{
    if (!env)
        return;

    g_free (env->display);
    g_free (env->wayland_display);
    g_free (env->xdg_runtime_dir);
    g_free (env->xauthority);
    g_free (env->dbus_session_bus_address);
    g_free (env->home);
    g_free (env->username);
    g_free (env);
}

static gchar *
read_env_from_proc (pid_t pid, const gchar *var_name)
{
    g_autofree gchar *environ_path = NULL;
    g_autofree gchar *contents = NULL;
    gsize length = 0;
    g_autoptr(GError) error = NULL;

    environ_path = g_strdup_printf ("/proc/%d/environ", pid);

    if (!g_file_get_contents (environ_path, &contents, &length, &error)) {
        g_debug ("Failed to read %s: %s", environ_path, error->message);
        return NULL;
    }

    /* Environment variables are null-separated */
    gsize prefix_len = strlen (var_name);
    for (gsize i = 0; i < length; ) {
        const gchar *entry = contents + i;
        gsize entry_len = strlen (entry);

        if (g_str_has_prefix (entry, var_name) &&
            entry_len > prefix_len &&
            entry[prefix_len] == '=') {
            return g_strdup (entry + prefix_len + 1);
        }

        i += entry_len + 1;
    }

    return NULL;
}

static pid_t
find_session_leader_pid (uid_t uid)
{
    g_autoptr(GSubprocess) subprocess = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *stdout_buf = NULL;
    g_autofree gchar *stderr_buf = NULL;
    g_autofree gchar *uid_str = NULL;
    pid_t pid = -1;

    uid_str = g_strdup_printf ("%d", uid);

    /* Use loginctl to find sessions for this user */
    subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                   G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                   &error,
                                   "loginctl", "list-sessions", "--no-legend", "-o", "json",
                                   NULL);
    if (!subprocess) {
        g_debug ("Failed to run loginctl: %s", error->message);

        /* Fallback: try to find a gnome-session or similar process */
        const gchar *session_procs[] = {
            "gnome-session-binary",
            "gnome-session",
            "gnome-shell",
            "plasma-shell",
            "xfce4-session",
            NULL
        };

        for (gint i = 0; session_procs[i]; i++) {
            g_autofree gchar *cmd = g_strdup_printf (
                "pgrep -u %d -x %s 2>/dev/null | head -1",
                uid, session_procs[i]);

            g_autofree gchar *output = NULL;
            if (g_spawn_command_line_sync (cmd, &output, NULL, NULL, NULL) &&
                output && *output) {
                pid = (pid_t) g_ascii_strtoll (g_strstrip (output), NULL, 10);
                if (pid > 0) {
                    g_debug ("Found session process %s with PID %d", session_procs[i], pid);
                    return pid;
                }
            }
        }

        return -1;
    }

    if (!g_subprocess_communicate_utf8 (subprocess, NULL, NULL, &stdout_buf, &stderr_buf, &error)) {
        g_debug ("Failed to get loginctl output: %s", error->message);
        return -1;
    }

    /* Try to parse JSON output */
    if (stdout_buf && *stdout_buf) {
        g_autoptr(GRegex) regex = g_regex_new ("\"leader\"\\s*:\\s*(\\d+)", 0, 0, NULL);
        GMatchInfo *match_info = NULL;

        if (g_regex_match (regex, stdout_buf, 0, &match_info)) {
            g_autofree gchar *pid_str = g_match_info_fetch (match_info, 1);
            pid = (pid_t) g_ascii_strtoll (pid_str, NULL, 10);
            g_debug ("Found session leader PID: %d", pid);
        }
        g_match_info_free (match_info);
    }

    return pid;
}

static uid_t
find_graphical_session_uid (void)
{
    g_autoptr(GSubprocess) subprocess = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *stdout_buf = NULL;
    uid_t uid = (uid_t) -1;

    /* Try loginctl to find active graphical session */
    subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                   G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                   &error,
                                   "loginctl", "list-sessions", "--no-legend",
                                   NULL);
    if (!subprocess) {
        g_debug ("loginctl not available: %s", error->message);
        goto fallback;
    }

    if (!g_subprocess_communicate_utf8 (subprocess, NULL, NULL, &stdout_buf, NULL, &error)) {
        g_debug ("Failed to get loginctl output: %s", error->message);
        goto fallback;
    }

    if (stdout_buf && *stdout_buf) {
        g_auto(GStrv) lines = g_strsplit (stdout_buf, "\n", -1);

        for (gint i = 0; lines[i] && *lines[i]; i++) {
            g_auto(GStrv) parts = g_strsplit_set (g_strstrip (lines[i]), " \t", -1);
            gint part_count = g_strv_length (parts);

            if (part_count >= 3) {
                /* Format: SESSION UID USER SEAT TTY */
                const gchar *session_id = parts[0];

                /* Check if this is a graphical session */
                g_autoptr(GSubprocess) type_proc = NULL;
                g_autofree gchar *type_output = NULL;

                type_proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE,
                                             NULL,
                                             "loginctl", "show-session",
                                             session_id, "-p", "Type", "--value",
                                             NULL);
                if (type_proc &&
                    g_subprocess_communicate_utf8 (type_proc, NULL, NULL, &type_output, NULL, NULL)) {
                    if (type_output) {
                        g_strstrip (type_output);
                        if (g_strcmp0 (type_output, "x11") == 0 ||
                            g_strcmp0 (type_output, "wayland") == 0) {
                            uid = (uid_t) g_ascii_strtoll (parts[1], NULL, 10);
                            g_debug ("Found graphical session %s for UID %d", session_id, uid);
                            return uid;
                        }
                    }
                }
            }
        }
    }

fallback:
    /* Fallback: look for common display managers or desktop environments */
    {
        g_autofree gchar *output = NULL;

        /* Find first non-root user running a display server */
        if (g_spawn_command_line_sync (
                "ps -eo uid,comm --no-headers | "
                "grep -E '(Xorg|Xwayland|gnome-shell|gnome-session|kwin)' | "
                "awk '$1 >= 1000 {print $1; exit}'",
                &output, NULL, NULL, NULL) && output && *output) {
            uid = (uid_t) g_ascii_strtoll (g_strstrip (output), NULL, 10);
            if (uid >= 1000) {
                g_debug ("Fallback: found UID %d from process list", uid);
                return uid;
            }
        }
    }

    /* Last resort: use SUDO_UID if available */
    const gchar *sudo_uid = g_getenv ("SUDO_UID");
    if (sudo_uid) {
        uid = (uid_t) g_ascii_strtoll (sudo_uid, NULL, 10);
        g_debug ("Using SUDO_UID: %d", uid);
        return uid;
    }

    /* Check /run/user for non-root users */
    {
        g_autoptr(GDir) dir = g_dir_open ("/run/user", 0, NULL);
        if (dir) {
            const gchar *name;
            while ((name = g_dir_read_name (dir)) != NULL) {
                uid_t test_uid = (uid_t) g_ascii_strtoll (name, NULL, 10);
                if (test_uid >= 1000) {
                    uid = test_uid;
                    g_debug ("Found user runtime dir for UID %d", uid);
                    break;
                }
            }
        }
    }

    return uid;
}

VpnSsoSessionEnv *
vpn_sso_get_graphical_session_env (void)
{
    VpnSsoSessionEnv *env;
    uid_t uid;
    struct passwd *pw;
    pid_t session_pid;

    /* First check if we already have display environment (not root) */
    if (getuid () != 0 && g_getenv ("DISPLAY")) {
        env = g_new0 (VpnSsoSessionEnv, 1);
        env->uid = getuid ();
        env->display = g_strdup (g_getenv ("DISPLAY"));
        env->wayland_display = g_strdup (g_getenv ("WAYLAND_DISPLAY"));
        env->xdg_runtime_dir = g_strdup (g_getenv ("XDG_RUNTIME_DIR"));
        env->xauthority = g_strdup (g_getenv ("XAUTHORITY"));
        env->dbus_session_bus_address = g_strdup (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
        env->home = g_strdup (g_getenv ("HOME"));

        pw = getpwuid (env->uid);
        if (pw)
            env->username = g_strdup (pw->pw_name);

        g_debug ("Using current process environment (not root)");
        return env;
    }

    /* We're running as root, need to find user's graphical session */
    uid = find_graphical_session_uid ();
    if (uid == (uid_t) -1 || uid < 1000) {
        g_warning ("Could not find graphical session UID");
        return NULL;
    }

    pw = getpwuid (uid);
    if (!pw) {
        g_warning ("Could not get passwd entry for UID %d", uid);
        return NULL;
    }

    env = g_new0 (VpnSsoSessionEnv, 1);
    env->uid = uid;
    env->username = g_strdup (pw->pw_name);
    env->home = g_strdup (pw->pw_dir);
    env->xdg_runtime_dir = g_strdup_printf ("/run/user/%d", uid);

    /* Try to find session leader process to read environment from */
    session_pid = find_session_leader_pid (uid);
    if (session_pid > 0) {
        env->display = read_env_from_proc (session_pid, "DISPLAY");
        env->wayland_display = read_env_from_proc (session_pid, "WAYLAND_DISPLAY");
        env->dbus_session_bus_address = read_env_from_proc (session_pid, "DBUS_SESSION_BUS_ADDRESS");
        env->xauthority = read_env_from_proc (session_pid, "XAUTHORITY");

        if (!env->xdg_runtime_dir || !g_file_test (env->xdg_runtime_dir, G_FILE_TEST_IS_DIR)) {
            g_free (env->xdg_runtime_dir);
            env->xdg_runtime_dir = read_env_from_proc (session_pid, "XDG_RUNTIME_DIR");
        }
    }

    /* Fallbacks for display */
    if (!env->display && !env->wayland_display) {
        /* Check for common X11 display */
        if (g_file_test ("/tmp/.X11-unix/X0", G_FILE_TEST_EXISTS))
            env->display = g_strdup (":0");
        else if (g_file_test ("/tmp/.X11-unix/X1", G_FILE_TEST_EXISTS))
            env->display = g_strdup (":1");
    }

    /* Fallback for XAUTHORITY */
    if (!env->xauthority) {
        /* First, try to find Xwayland process and read XAUTHORITY from it
         * (needed for Wayland sessions running XWayland) */
        g_autofree gchar *pgrep_cmd = g_strdup_printf ("pgrep -u %d Xwayland 2>/dev/null | head -1", uid);
        g_autofree gchar *xwayland_pid_str = NULL;

        if (g_spawn_command_line_sync (pgrep_cmd, &xwayland_pid_str, NULL, NULL, NULL) &&
            xwayland_pid_str && *xwayland_pid_str) {
            pid_t xwayland_pid = (pid_t) g_ascii_strtoll (g_strstrip (xwayland_pid_str), NULL, 10);
            if (xwayland_pid > 0) {
                env->xauthority = read_env_from_proc (xwayland_pid, "XAUTHORITY");
                if (env->xauthority)
                    g_debug ("Found XAUTHORITY from Xwayland process: %s", env->xauthority);
            }
        }

        /* If still not found, try to glob for mutter Xwayland auth files */
        if (!env->xauthority && env->xdg_runtime_dir) {
            g_autofree gchar *glob_pattern = g_strdup_printf ("%s/.mutter-Xwaylandauth.*", env->xdg_runtime_dir);
            glob_t globbuf = { 0 };

            if (glob (glob_pattern, GLOB_NOSORT, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
                env->xauthority = g_strdup (globbuf.gl_pathv[0]);
                g_debug ("Found XAUTHORITY from glob: %s", env->xauthority);
            }
            globfree (&globbuf);
        }

        /* Last fallback: traditional ~/.Xauthority */
        if (!env->xauthority && env->home) {
            g_autofree gchar *xauth_path = g_strdup_printf ("%s/.Xauthority", env->home);
            if (g_file_test (xauth_path, G_FILE_TEST_EXISTS))
                env->xauthority = g_steal_pointer (&xauth_path);
        }
    }

    /* Fallback for D-Bus session bus */
    if (!env->dbus_session_bus_address && env->xdg_runtime_dir) {
        g_autofree gchar *bus_path = g_strdup_printf ("%s/bus", env->xdg_runtime_dir);
        if (g_file_test (bus_path, G_FILE_TEST_EXISTS))
            env->dbus_session_bus_address = g_strdup_printf ("unix:path=%s", bus_path);
    }

    g_debug ("Session environment for UID %d:", uid);
    g_debug ("  DISPLAY=%s", env->display ? env->display : "(null)");
    g_debug ("  WAYLAND_DISPLAY=%s", env->wayland_display ? env->wayland_display : "(null)");
    g_debug ("  XDG_RUNTIME_DIR=%s", env->xdg_runtime_dir ? env->xdg_runtime_dir : "(null)");
    g_debug ("  XAUTHORITY=%s", env->xauthority ? env->xauthority : "(null)");
    g_debug ("  DBUS_SESSION_BUS_ADDRESS=%s", env->dbus_session_bus_address ? env->dbus_session_bus_address : "(null)");
    g_debug ("  HOME=%s", env->home ? env->home : "(null)");

    return env;
}
