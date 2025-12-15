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
#include "credential-cache.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <arpa/inet.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <NetworkManager.h>

/* Private data accessor - uses modern G_DEFINE_TYPE_WITH_PRIVATE */

/* Configuration keys for VPN connection data */
#define NM_VPN_SSO_KEY_GATEWAY           "gateway"
#define NM_VPN_SSO_KEY_PROTOCOL          "protocol"
#define NM_VPN_SSO_KEY_USERNAME          "username"
#define NM_VPN_SSO_KEY_USERGROUP         "usergroup"
#define NM_VPN_SSO_KEY_EXTRA_ARGS        "extra-args"
#define NM_VPN_SSO_KEY_CACHE_HOURS       "cache-hours"
#define NM_VPN_SSO_KEY_EXTERNAL_BROWSER  "external-browser"

/* Protocol types */
#define NM_VPN_SSO_PROTOCOL_GP      "globalprotect"
#define NM_VPN_SSO_PROTOCOL_AC      "anyconnect"

/* Bundled SSO tool paths - use absolute paths to avoid conflicts with user installations */
#define BUNDLED_GP_SAML_GUI         "/opt/gnome-vpn-sso/bin/gp-saml-gui"
#define BUNDLED_OPENCONNECT_SSO     "/opt/gnome-vpn-sso/bin/openconnect-sso"

/* Process watch interval (ms) */
#define PROCESS_WATCH_INTERVAL 1000

typedef enum {
    VPN_STATE_IDLE,
    VPN_STATE_AUTHENTICATING,
    VPN_STATE_CONNECTING,
    VPN_STATE_CONNECTED,
    VPN_STATE_DISCONNECTING,
    VPN_STATE_FAILED
} VpnConnectionState;

struct _NmVpnSsoServicePrivate {
    /* Connection state */
    VpnConnectionState state;

    /* VPN configuration */
    char *gateway;
    char *protocol;
    char *username;
    char *usergroup;
    char *extra_args;
    gint cache_hours;
    gboolean external_browser;

    /* SSO authentication */
    char *sso_cookie;
    char *sso_fingerprint;   /* Server certificate fingerprint (AnyConnect) */
    GPid sso_pid;
    GIOChannel *sso_stdout;
    GIOChannel *sso_stderr;
    guint sso_stdout_watch;
    guint sso_stderr_watch;
    guint sso_child_watch;
    GString *sso_output;

    /* OpenConnect process */
    GPid openconnect_pid;
    GIOChannel *openconnect_stdout;
    GIOChannel *openconnect_stderr;
    GIOChannel *openconnect_stdin;
    guint openconnect_stdout_watch;
    guint openconnect_stderr_watch;
    guint openconnect_child_watch;
    guint openconnect_watch_timer;

    /* IP4 configuration */
    char *tundev;          /* Tunnel device name (e.g., "tun0") */
    char *ip4_address;
    char *ip4_netmask;
    char *ip4_gateway;
    GPtrArray *ip4_dns;
    GPtrArray *ip4_routes;

    /* IP4 config reporting retry mechanism */
    guint ip4_config_retry_source;
    gint ip4_config_retry_count;

    /* Cached credential tracking for fallback to SSO */
    gboolean using_cached_credentials;
};

G_DEFINE_TYPE_WITH_PRIVATE (NmVpnSsoService, nm_vpn_sso_service, NM_TYPE_VPN_SERVICE_PLUGIN)

/* Forward declarations */
typedef struct _SsoChildSetupData SsoChildSetupData;
static gboolean connect_to_vpn (NmVpnSsoService *self, GError **error);
static void cleanup_connection (NmVpnSsoService *self);
static void start_sso_authentication (NmVpnSsoService *self);
static void start_openconnect (NmVpnSsoService *self);
static gchar **build_subprocess_environment (SsoChildSetupData **out_setup_data);

/*
 * Credential cache callbacks
 */
static void
credential_store_cb (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
    g_autoptr(GError) error = NULL;

    if (!vpn_sso_credential_cache_store_finish (result, &error)) {
        g_warning ("Failed to store credentials in cache: %s", error->message);
    } else {
        g_message ("Credentials stored in secure cache");
    }
}

static void
store_credentials_in_cache (NmVpnSsoService *self)
{
    NmVpnSsoServicePrivate *priv = self->priv;

    if (!priv->sso_cookie || !*priv->sso_cookie) {
        g_debug ("No cookie to cache");
        return;
    }

    g_message ("Storing SSO credentials in cache for %s (%s) - expires in %d hours",
               priv->gateway, priv->protocol,
               priv->cache_hours > 0 ? priv->cache_hours : VPN_SSO_DEFAULT_CACHE_DURATION_HOURS);

    vpn_sso_credential_cache_store_async (priv->gateway,
                                          priv->protocol,
                                          priv->username,
                                          priv->sso_cookie,
                                          priv->sso_fingerprint,
                                          priv->usergroup,
                                          priv->cache_hours,
                                          NULL, /* cancellable */
                                          credential_store_cb,
                                          self);
}

static void
credential_lookup_cb (GObject      *source,
                      GAsyncResult *result,
                      gpointer      user_data)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (user_data);
    NmVpnSsoServicePrivate *priv = self->priv;
    g_autoptr(GError) error = NULL;
    g_autoptr(VpnSsoCachedCredential) cached = NULL;

    cached = vpn_sso_credential_cache_lookup_finish (result, &error);

    if (error) {
        g_warning ("Cache lookup failed: %s - proceeding with SSO", error->message);
        start_sso_authentication (self);
        return;
    }

    if (cached && cached->cookie && *cached->cookie) {
        g_message ("Found valid cached credentials for %s (%s) - skipping SSO",
                   priv->gateway, priv->protocol);
        g_message ("  cached cookie length: %zu", strlen (cached->cookie));
        g_message ("  cached fingerprint: %s", cached->fingerprint ? cached->fingerprint : "(null)");
        g_message ("  cached username: %s", cached->username ? cached->username : "(null)");
        g_message ("  cached usergroup: %s", cached->usergroup ? cached->usergroup : "(null)");

        /* Use cached credentials */
        g_free (priv->sso_cookie);
        priv->sso_cookie = g_strdup (cached->cookie);

        if (cached->fingerprint) {
            g_free (priv->sso_fingerprint);
            priv->sso_fingerprint = g_strdup (cached->fingerprint);
        }

        if (cached->username && !priv->username) {
            priv->username = g_strdup (cached->username);
        }

        /* Always use cached usergroup - it contains the correct usergroup
         * for the cached cookie type (e.g., portal:portal-userauthcookie)
         */
        if (cached->usergroup) {
            g_free (priv->usergroup);
            priv->usergroup = g_strdup (cached->usergroup);
            g_message ("Using cached usergroup: %s", priv->usergroup);
        }

        /* Mark that we're using cached credentials for fallback handling */
        priv->using_cached_credentials = TRUE;

        /* Skip SSO and go directly to OpenConnect */
        priv->state = VPN_STATE_CONNECTING;
        start_openconnect (self);
    } else {
        g_message ("No valid cached credentials found for %s (%s) - starting SSO",
                   priv->gateway, priv->protocol);
        priv->using_cached_credentials = FALSE;
        start_sso_authentication (self);
    }
}

/*
 * SSO Authentication Handlers
 */

static gboolean
sso_stdout_cb (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (user_data);
    NmVpnSsoServicePrivate *priv = self->priv;
    gchar buf[1024];
    gsize bytes_read;
    GIOStatus status;

    if (condition & G_IO_IN) {
        status = g_io_channel_read_chars (source, buf, sizeof (buf) - 1, &bytes_read, NULL);
        if (status == G_IO_STATUS_NORMAL && bytes_read > 0) {
            buf[bytes_read] = '\0';
            g_string_append (priv->sso_output, buf);
            g_message ("SSO output: %s", buf);

            /* For AnyConnect, openconnect-sso handles the full connection.
             * Log progress indicators but DON'T report IP4 config yet -
             * the openconnect stdout/stderr callbacks will handle that
             * after "Configured as" appears and the tun device exists. */
            if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_AC) == 0) {
                if (strstr (buf, "Connected to") != NULL ||
                    strstr (buf, "Established DTLS") != NULL ||
                    strstr (buf, "ESP session established") != NULL) {
                    g_message ("AnyConnect connection progress: %s", buf);
                    /* Don't set state or report config here - wait for "Configured as" */
                }
            }
        }
    }

    if (condition & G_IO_HUP) {
        return FALSE;
    }

    return TRUE;
}

static gboolean
sso_stderr_cb (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (user_data);
    NmVpnSsoServicePrivate *priv = self->priv;
    gchar buf[1024];
    gsize bytes_read;
    GIOStatus status;

    if (condition & G_IO_IN) {
        status = g_io_channel_read_chars (source, buf, sizeof (buf) - 1, &bytes_read, NULL);
        if (status == G_IO_STATUS_NORMAL && bytes_read > 0) {
            buf[bytes_read] = '\0';
            g_message ("SSO stderr: %s", buf);

            /* For AnyConnect, log connection progress from stderr.
             * Don't report IP4 config here - the openconnect callbacks
             * will handle that after "Configured as" and tun device exists. */
            if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_AC) == 0) {
                if (strstr (buf, "Connected to") != NULL ||
                    strstr (buf, "Established DTLS") != NULL ||
                    strstr (buf, "ESP session established") != NULL) {
                    g_message ("AnyConnect connection progress (stderr): %s", buf);
                }
            }
        }
    }

    if (condition & G_IO_HUP) {
        return FALSE;
    }

    return TRUE;
}

static void
parse_sso_cookie (NmVpnSsoService *self, const char *output)
{
    NmVpnSsoServicePrivate *priv = self->priv;
    const char *cookie_start, *cookie_end;

    /* For GlobalProtect, gp-saml-gui outputs credentials in this format:
     * HOST=https://vpn.example.com/globalprotect
     * USER=username
     * COOKIE=<prelogin-cookie-value>
     * OS=linux-64
     */

    if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_GP) == 0) {
        /* Look for COOKIE= (gp-saml-gui standard output format) */
        cookie_start = strstr (output, "COOKIE=");
        if (cookie_start) {
            cookie_start += 7; /* Skip "COOKIE=" */
            cookie_end = strchr (cookie_start, '\n');
            if (cookie_end) {
                g_free (priv->sso_cookie);
                priv->sso_cookie = g_strndup (cookie_start, cookie_end - cookie_start);
            } else {
                /* No newline - cookie goes to end of string */
                g_free (priv->sso_cookie);
                priv->sso_cookie = g_strdup (cookie_start);
            }

            /* gp-saml-gui uses shlex.quote() which may add single quotes around
             * values with shell special characters - strip them if present */
            if (priv->sso_cookie && priv->sso_cookie[0] == '\'') {
                gsize len = strlen (priv->sso_cookie);
                if (len > 1 && priv->sso_cookie[len - 1] == '\'') {
                    gchar *unquoted = g_strndup (priv->sso_cookie + 1, len - 2);
                    g_free (priv->sso_cookie);
                    priv->sso_cookie = unquoted;
                }
            }
            g_message ("Extracted GlobalProtect cookie: %s", priv->sso_cookie);
        } else {
            /* Fallback: look for prelogin-cookie= format (alternative output) */
            cookie_start = strstr (output, "prelogin-cookie=");
            if (cookie_start) {
                cookie_start += 16; /* Skip "prelogin-cookie=" */
                cookie_end = strchr (cookie_start, '\n');
                if (cookie_end) {
                    g_free (priv->sso_cookie);
                    priv->sso_cookie = g_strndup (cookie_start, cookie_end - cookie_start);
                } else {
                    g_free (priv->sso_cookie);
                    priv->sso_cookie = g_strdup (cookie_start);
                }
                g_message ("Extracted GlobalProtect cookie (prelogin-cookie format): %s", priv->sso_cookie);
            } else {
                g_warning ("GlobalProtect: No cookie found in SSO output. Expected COOKIE= or prelogin-cookie=");
                g_warning ("SSO output was: %s", output);
            }
        }
    } else if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_AC) == 0) {
        /* For AnyConnect, openconnect-sso --authenticate outputs:
         * HOST=https://vpn.example.com/
         * COOKIE=<webvpn-cookie-value>
         * FINGERPRINT=<server-cert-fingerprint>
         */
        char **lines = g_strsplit (output, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            char *line = g_strstrip (lines[i]);

            if (g_str_has_prefix (line, "COOKIE=")) {
                g_free (priv->sso_cookie);
                priv->sso_cookie = g_strdup (line + 7); /* Skip "COOKIE=" */
                g_message ("Extracted AnyConnect cookie: %s", priv->sso_cookie);
            } else if (g_str_has_prefix (line, "FINGERPRINT=")) {
                g_free (priv->sso_fingerprint);
                priv->sso_fingerprint = g_strdup (line + 12); /* Skip "FINGERPRINT=" */
                g_message ("Extracted server fingerprint: %s", priv->sso_fingerprint);
            } else if (g_str_has_prefix (line, "HOST=")) {
                /* HOST from openconnect-sso may be different from our gateway
                 * (e.g., includes protocol), but we already have the gateway
                 * from configuration so we just log it */
                g_message ("AnyConnect HOST: %s", line + 5);
            }
        }
        g_strfreev (lines);
    }
}

static void
sso_child_watch_cb (GPid pid, gint status, gpointer user_data)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (user_data);
    NmVpnSsoServicePrivate *priv = self->priv;

    g_message ("SSO process exited with status %d", status);

    /* Clean up SSO process resources */
    if (priv->sso_stdout_watch) {
        g_source_remove (priv->sso_stdout_watch);
        priv->sso_stdout_watch = 0;
    }
    if (priv->sso_stderr_watch) {
        g_source_remove (priv->sso_stderr_watch);
        priv->sso_stderr_watch = 0;
    }
    if (priv->sso_stdout) {
        g_io_channel_unref (priv->sso_stdout);
        priv->sso_stdout = NULL;
    }
    if (priv->sso_stderr) {
        g_io_channel_unref (priv->sso_stderr);
        priv->sso_stderr = NULL;
    }

    g_spawn_close_pid (pid);
    priv->sso_pid = 0;
    priv->sso_child_watch = 0;

    /*
     * Both protocols now use the same flow:
     * 1. SSO tool handles authentication and outputs credentials
     * 2. We parse the credentials (cookie, fingerprint, etc.)
     * 3. We spawn openconnect with those credentials to establish the tunnel
     *
     * For GlobalProtect: gp-saml-gui outputs SAML cookie
     * For AnyConnect: openconnect-sso --authenticate outputs HOST, COOKIE, FINGERPRINT
     */
    if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_AC) == 0) {
        /* AnyConnect: parse credentials from openconnect-sso --authenticate output */
        if (WIFEXITED (status) && WEXITSTATUS (status) == 0) {
            /* SSO authentication succeeded - parse credentials and spawn openconnect */
            parse_sso_cookie (self, priv->sso_output->str);

            if (priv->sso_cookie) {
                g_message ("AnyConnect SSO successful, starting OpenConnect with cookie");
                /* Store credentials in cache for future connections */
                store_credentials_in_cache (self);
                /* Fresh credentials from SSO - don't retry SSO if these fail */
                priv->using_cached_credentials = FALSE;
                priv->state = VPN_STATE_CONNECTING;
                start_openconnect (self);
            } else {
                g_warning ("AnyConnect SSO completed but no cookie found");
                nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (self),
                                              NM_VPN_PLUGIN_FAILURE_LOGIN_FAILED);
                cleanup_connection (self);
            }
        } else {
            /* Failed or cancelled */
            g_warning ("AnyConnect SSO authentication failed (exit status %d)",
                       WIFEXITED (status) ? WEXITSTATUS (status) : -1);
            nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (self),
                                          NM_VPN_PLUGIN_FAILURE_LOGIN_FAILED);
            cleanup_connection (self);
        }
    } else {
        /* GlobalProtect: parse cookie and spawn openconnect */
        if (WIFEXITED (status) && WEXITSTATUS (status) == 0) {
            /* SSO authentication succeeded */
            parse_sso_cookie (self, priv->sso_output->str);

            if (priv->sso_cookie) {
                g_message ("SSO authentication successful, starting OpenConnect");
                /* Store credentials in cache for future connections */
                store_credentials_in_cache (self);
                /* Fresh credentials from SSO - don't retry SSO if these fail */
                priv->using_cached_credentials = FALSE;
                priv->state = VPN_STATE_CONNECTING;
                start_openconnect (self);
            } else {
                g_warning ("SSO authentication completed but no cookie found");
                nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (self),
                                              NM_VPN_PLUGIN_FAILURE_LOGIN_FAILED);
                cleanup_connection (self);
            }
        } else {
            g_warning ("SSO authentication failed");
            nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (self),
                                          NM_VPN_PLUGIN_FAILURE_LOGIN_FAILED);
            cleanup_connection (self);
        }
    }

    if (priv->sso_output) {
        g_string_free (priv->sso_output, TRUE);
        priv->sso_output = NULL;
    }
}

/**
 * SsoChildSetupData:
 *
 * Data passed to the child_setup function for dropping privileges
 * when spawning SSO subprocesses. Also used to free the data after spawn.
 */
struct _SsoChildSetupData {
    uid_t uid;
    gid_t gid;
    gchar *home;  /* Owned by this struct, free with g_free */
};

static void
sso_child_setup_data_free (SsoChildSetupData *data)
{
    if (data) {
        g_free (data->home);
        g_free (data);
    }
}

/**
 * sso_child_setup:
 *
 * Child setup function called after fork() but before exec().
 * Drops privileges from root to the target user so that the
 * Qt WebEngine browser can run (Chromium refuses to run as root).
 */
static void
sso_child_setup (gpointer user_data)
{
    SsoChildSetupData *data = user_data;

    if (data == NULL)
        return;

    /* Only drop privileges if we're running as root */
    if (getuid () != 0)
        return;

    /* Clear supplementary groups */
    if (setgroups (0, NULL) < 0) {
        g_warning ("Failed to clear supplementary groups: %s", g_strerror (errno));
        /* Continue anyway */
    }

    /* Set the group ID first (must be done before setuid) */
    if (setgid (data->gid) < 0) {
        g_warning ("Failed to setgid(%d): %s", data->gid, g_strerror (errno));
        _exit (1);
    }

    /* Set the user ID */
    if (setuid (data->uid) < 0) {
        g_warning ("Failed to setuid(%d): %s", data->uid, g_strerror (errno));
        _exit (1);
    }

    /* Change to user's home directory */
    if (data->home && chdir (data->home) < 0) {
        /* Non-fatal - just log it */
        g_debug ("Failed to chdir to %s: %s", data->home, g_strerror (errno));
    }
}

/**
 * build_subprocess_environment:
 *
 * Builds an environment array for spawning GUI subprocesses.
 * This is needed because the service runs as root but needs to
 * display GUI windows in the user's graphical session.
 *
 * @out_setup_data: (out) (optional): If non-NULL, returns a
 *                  SsoChildSetupData struct that should be passed
 *                  to sso_child_setup. Caller should free with g_free().
 *
 * Returns: (transfer full): A null-terminated array of environment
 *          strings, or NULL on failure. Free with g_strfreev().
 */
static gchar **
build_subprocess_environment (SsoChildSetupData **out_setup_data)
{
    g_autoptr(VpnSsoSessionEnv) session_env = NULL;
    GPtrArray *env_array;
    const gchar *path;
    g_autofree gchar *new_path = NULL;
    struct passwd *pw;

    if (out_setup_data)
        *out_setup_data = NULL;

    session_env = vpn_sso_get_graphical_session_env ();
    if (!session_env) {
        g_warning ("Could not detect graphical session environment");
        return NULL;
    }

    /* Populate the child setup data for dropping privileges */
    if (out_setup_data && session_env->uid >= 1000) {
        SsoChildSetupData *setup_data = g_new0 (SsoChildSetupData, 1);
        setup_data->uid = session_env->uid;
        setup_data->home = g_strdup (session_env->home);  /* Copy - session_env will be freed */

        /* Get the user's primary GID from passwd */
        pw = getpwuid (session_env->uid);
        if (pw) {
            setup_data->gid = pw->pw_gid;
        } else {
            /* Fallback: use the UID as GID (common on single-user systems) */
            setup_data->gid = session_env->uid;
        }

        *out_setup_data = setup_data;
        g_message ("Will run SSO subprocess as UID %d, GID %d", setup_data->uid, setup_data->gid);
    }

    env_array = g_ptr_array_new ();

    /* Add display environment variables */
    if (session_env->display)
        g_ptr_array_add (env_array, g_strdup_printf ("DISPLAY=%s", session_env->display));

    if (session_env->wayland_display)
        g_ptr_array_add (env_array, g_strdup_printf ("WAYLAND_DISPLAY=%s", session_env->wayland_display));

    if (session_env->xdg_runtime_dir)
        g_ptr_array_add (env_array, g_strdup_printf ("XDG_RUNTIME_DIR=%s", session_env->xdg_runtime_dir));

    if (session_env->xauthority)
        g_ptr_array_add (env_array, g_strdup_printf ("XAUTHORITY=%s", session_env->xauthority));

    if (session_env->dbus_session_bus_address)
        g_ptr_array_add (env_array, g_strdup_printf ("DBUS_SESSION_BUS_ADDRESS=%s", session_env->dbus_session_bus_address));

    if (session_env->home)
        g_ptr_array_add (env_array, g_strdup_printf ("HOME=%s", session_env->home));

    if (session_env->username)
        g_ptr_array_add (env_array, g_strdup_printf ("USER=%s", session_env->username));

    /* Build PATH with our bundled tools directory first */
    path = g_getenv ("PATH");
    if (path)
        new_path = g_strdup_printf ("/opt/gnome-vpn-sso/bin:%s", path);
    else
        new_path = g_strdup ("/opt/gnome-vpn-sso/bin:/usr/local/bin:/usr/bin:/bin");

    g_ptr_array_add (env_array, g_strdup_printf ("PATH=%s", new_path));

    /* Add some standard environment variables that Python/Qt might need */
    g_ptr_array_add (env_array, g_strdup ("QT_QPA_PLATFORM=xcb"));
    g_ptr_array_add (env_array, g_strdup ("GDK_BACKEND=x11"));

    /* Null-terminate the array */
    g_ptr_array_add (env_array, NULL);

    g_message ("Built subprocess environment: DISPLAY=%s, WAYLAND_DISPLAY=%s, XDG_RUNTIME_DIR=%s, HOME=%s",
               session_env->display ? session_env->display : "(null)",
               session_env->wayland_display ? session_env->wayland_display : "(null)",
               session_env->xdg_runtime_dir ? session_env->xdg_runtime_dir : "(null)",
               session_env->home ? session_env->home : "(null)");

    return (gchar **) g_ptr_array_free (env_array, FALSE);
}

static void
start_sso_authentication (NmVpnSsoService *self)
{
    NmVpnSsoServicePrivate *priv = self->priv;
    GError *error = NULL;
    GPtrArray *argv;
    gchar **envp;
    gint sso_stdout_fd, sso_stderr_fd;

    g_message ("Starting SSO authentication for protocol: %s", priv->protocol);

    priv->sso_output = g_string_new ("");
    argv = g_ptr_array_new ();

    if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_GP) == 0) {
        /* GlobalProtect: use bundled gp-saml-gui with absolute path */
        g_ptr_array_add (argv, (gpointer) BUNDLED_GP_SAML_GUI);
        g_ptr_array_add (argv, (gpointer) "--portal");
        g_ptr_array_add (argv, (gpointer) priv->gateway);
        /* Use external browser if configured (allows password manager integration) */
        if (priv->external_browser) {
            g_ptr_array_add (argv, (gpointer) "--external");
            g_message ("Using external browser for SSO authentication");
        }
        g_ptr_array_add (argv, (gpointer) "--");
        g_ptr_array_add (argv, (gpointer) "--protocol=gp");
    } else if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_AC) == 0) {
        /* AnyConnect: use bundled openconnect-sso with absolute path
         *
         * NOTE: We intentionally do NOT pass --user here, even if username is configured.
         * Passing --user triggers openconnect-sso to prompt for password via getpass(),
         * which fails when running as a service (no TTY). For pure SSO authentication,
         * the user is identified through the browser-based SSO flow.
         *
         * We use --authenticate to get credentials only - openconnect-sso will print
         * the cookie/credentials and exit without trying to run openconnect itself.
         * Our service then uses these credentials to establish the VPN connection.
         *
         * We also force --browser-display-mode=shown to ensure the SSO browser window
         * appears for the user to complete authentication.
         */
        g_ptr_array_add (argv, (gpointer) BUNDLED_OPENCONNECT_SSO);
        g_ptr_array_add (argv, (gpointer) "--server");
        g_ptr_array_add (argv, (gpointer) priv->gateway);
        g_ptr_array_add (argv, (gpointer) "--authenticate");
        /* Use external browser if configured (allows password manager integration) */
        if (priv->external_browser) {
            g_ptr_array_add (argv, (gpointer) "--external-browser");
            g_message ("Using external browser for AnyConnect SSO authentication");
        } else {
            g_ptr_array_add (argv, (gpointer) "--browser-display-mode");
            g_ptr_array_add (argv, (gpointer) "shown");
        }
    } else {
        g_warning ("Unknown protocol: %s", priv->protocol);
        nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (self),
                                      NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
        g_ptr_array_free (argv, TRUE);
        return;
    }

    g_ptr_array_add (argv, NULL);

    /* Build environment with display variables for GUI and get user credentials
     * for dropping privileges (Qt WebEngine refuses to run as root) */
    SsoChildSetupData *setup_data = NULL;
    envp = build_subprocess_environment (&setup_data);
    if (!envp) {
        g_warning ("Failed to build subprocess environment - GUI may not work");
        /* Continue anyway, some systems might work without explicit env */
    }

    if (!g_spawn_async_with_pipes (NULL, /* working_directory */
                                   (gchar **) argv->pdata,
                                   envp,
                                   G_SPAWN_DO_NOT_REAP_CHILD, /* No SEARCH_PATH - using absolute paths */
                                   sso_child_setup, /* Drop privileges to user */
                                   setup_data, /* user_data for child_setup */
                                   &priv->sso_pid,
                                   NULL, /* stdin */
                                   &sso_stdout_fd,
                                   &sso_stderr_fd,
                                   &error)) {
        g_warning ("Failed to spawn SSO process: %s", error->message);
        nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (self),
                                      NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
        g_error_free (error);
        g_ptr_array_free (argv, TRUE);
        g_strfreev (envp);
        sso_child_setup_data_free (setup_data);
        return;
    }

    g_strfreev (envp);
    sso_child_setup_data_free (setup_data);

    g_message ("SSO process started with PID %d", priv->sso_pid);

    /* Set up I/O channels */
    priv->sso_stdout = g_io_channel_unix_new (sso_stdout_fd);
    priv->sso_stderr = g_io_channel_unix_new (sso_stderr_fd);
    g_io_channel_set_encoding (priv->sso_stdout, NULL, NULL);
    g_io_channel_set_encoding (priv->sso_stderr, NULL, NULL);
    g_io_channel_set_buffered (priv->sso_stdout, FALSE);
    g_io_channel_set_buffered (priv->sso_stderr, FALSE);

    priv->sso_stdout_watch = g_io_add_watch (priv->sso_stdout,
                                            G_IO_IN | G_IO_HUP,
                                            sso_stdout_cb,
                                            self);
    priv->sso_stderr_watch = g_io_add_watch (priv->sso_stderr,
                                            G_IO_IN | G_IO_HUP,
                                            sso_stderr_cb,
                                            self);

    priv->sso_child_watch = g_child_watch_add (priv->sso_pid,
                                              sso_child_watch_cb,
                                              self);

    priv->state = VPN_STATE_AUTHENTICATING;

    g_ptr_array_free (argv, TRUE);
}

/*
 * OpenConnect Process Handlers
 */

static void
parse_openconnect_output (NmVpnSsoService *self, const gchar *buf)
{
    NmVpnSsoServicePrivate *priv = self->priv;
    const gchar *p;

    /* Parse tunnel device name: look for "tun" followed by digits
     * Common patterns:
     *   "Connected tun0 as 10.x.x.x"
     *   "Interface: tun0"
     *   "Using tun0"
     */
    if (!priv->tundev) {
        p = buf;
        while ((p = strstr (p, "tun")) != NULL) {
            /* Check if followed by digits */
            const gchar *num_start = p + 3;
            if (*num_start >= '0' && *num_start <= '9') {
                /* Extract tun device name */
                const gchar *num_end = num_start;
                while (*num_end >= '0' && *num_end <= '9')
                    num_end++;
                gchar *tundev = g_strndup (p, num_end - p);
                priv->tundev = tundev;
                g_message ("Detected tunnel device: %s", priv->tundev);
                break;
            }
            p++;
        }
    }

    /* Parse IP address: look for "as X.X.X.X" or "Configured X.X.X.X" patterns */
    if (!priv->ip4_address) {
        /* Pattern: "as X.X.X.X" */
        p = strstr (buf, " as ");
        if (p) {
            p += 4; /* Skip " as " */
            /* Check if it looks like an IP address */
            if (*p >= '0' && *p <= '9') {
                const gchar *ip_end = p;
                while ((*ip_end >= '0' && *ip_end <= '9') || *ip_end == '.')
                    ip_end++;
                gchar *addr = g_strndup (p, ip_end - p);
                /* Validate it looks like an IP (has at least 3 dots) */
                gint dot_count = 0;
                for (const gchar *c = addr; *c; c++)
                    if (*c == '.') dot_count++;
                if (dot_count == 3) {
                    priv->ip4_address = addr;
                    g_message ("Detected VPN IP address: %s", priv->ip4_address);
                } else {
                    g_free (addr);
                }
            }
        }
    }

    /* Parse gateway IP: look for "Connected to X.X.X.X:port" pattern
     * OpenConnect outputs: "Connected to 147.86.3.240:443"
     */
    if (!priv->ip4_gateway) {
        p = strstr (buf, "Connected to ");
        if (p) {
            p += 13; /* Skip "Connected to " */
            /* Check if it looks like an IP address */
            if (*p >= '0' && *p <= '9') {
                const gchar *ip_end = p;
                /* Parse until we hit ':' (port separator) or non-IP char */
                while ((*ip_end >= '0' && *ip_end <= '9') || *ip_end == '.')
                    ip_end++;
                gchar *addr = g_strndup (p, ip_end - p);
                /* Validate it looks like an IP (has 3 dots) */
                gint dot_count = 0;
                for (const gchar *c = addr; *c; c++)
                    if (*c == '.') dot_count++;
                if (dot_count == 3) {
                    priv->ip4_gateway = addr;
                    g_message ("Detected VPN gateway IP: %s", priv->ip4_gateway);
                } else {
                    g_free (addr);
                }
            }
        }
    }

    /* Parse GlobalProtect portal-userauthcookie for session persistence.
     * This cookie is returned AFTER successful login and is valid for hours,
     * unlike the prelogin-cookie which expires in seconds/minutes.
     * OpenConnect outputs: "GlobalProtect login returned portal-userauthcookie=XXX"
     */
    if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_GP) == 0) {
        p = strstr (buf, "portal-userauthcookie=");
        if (p) {
            p += 22; /* Skip "portal-userauthcookie=" */
            /* Find end of cookie (newline or end of string) */
            const gchar *cookie_end = p;
            while (*cookie_end && *cookie_end != '\n' && *cookie_end != '\r' && *cookie_end != ' ')
                cookie_end++;

            /* Only update if we got a real cookie (not "empty") */
            if (cookie_end > p && g_ascii_strncasecmp (p, "empty", 5) != 0) {
                gchar *new_cookie = g_strndup (p, cookie_end - p);

                /* Check if this is different from what we have cached */
                if (g_strcmp0 (priv->sso_cookie, new_cookie) != 0) {
                    g_message ("Captured GlobalProtect portal-userauthcookie (length=%zu)",
                               strlen (new_cookie));

                    /* Update our stored cookie */
                    g_free (priv->sso_cookie);
                    priv->sso_cookie = new_cookie;

                    /* Update the usergroup for portal-userauthcookie.
                     * This is different from the prelogin-cookie usergroup.
                     */
                    g_free (priv->usergroup);
                    priv->usergroup = g_strdup ("portal:portal-userauthcookie");
                    g_message ("Updated usergroup to portal:portal-userauthcookie");

                    /* Update the credential cache with this long-lived cookie */
                    g_message ("Updating credential cache with portal-userauthcookie");
                    store_credentials_in_cache (self);
                } else {
                    g_free (new_cookie);
                }
            }
        }
    }

    /* Parse DNS servers: OpenConnect outputs "Got DNS server address X.X.X.X"
     * We need to collect all DNS servers as there may be multiple
     */
    p = buf;
    while ((p = strstr (p, "DNS server")) != NULL) {
        /* Look for the IP address after "address" */
        const gchar *addr_start = strstr (p, "address ");
        if (addr_start) {
            addr_start += 8; /* Skip "address " */
            /* Skip any whitespace */
            while (*addr_start == ' ')
                addr_start++;
            if (*addr_start >= '0' && *addr_start <= '9') {
                const gchar *addr_end = addr_start;
                while ((*addr_end >= '0' && *addr_end <= '9') || *addr_end == '.')
                    addr_end++;
                gchar *dns_addr = g_strndup (addr_start, addr_end - addr_start);
                /* Validate it looks like an IP (has 3 dots) */
                gint dot_count = 0;
                for (const gchar *c = dns_addr; *c; c++)
                    if (*c == '.') dot_count++;
                if (dot_count == 3) {
                    /* Check if we already have this DNS server */
                    gboolean already_have = FALSE;
                    if (priv->ip4_dns) {
                        for (guint i = 0; i < priv->ip4_dns->len; i++) {
                            if (g_strcmp0 (g_ptr_array_index (priv->ip4_dns, i), dns_addr) == 0) {
                                already_have = TRUE;
                                break;
                            }
                        }
                    }
                    if (!already_have) {
                        if (!priv->ip4_dns)
                            priv->ip4_dns = g_ptr_array_new_with_free_func (g_free);
                        g_ptr_array_add (priv->ip4_dns, dns_addr);
                        g_message ("Detected VPN DNS server: %s", dns_addr);
                    } else {
                        g_free (dns_addr);
                    }
                } else {
                    g_free (dns_addr);
                }
            }
        }
        p++; /* Move past current match to find more DNS servers */
    }
}

static void
report_ip4_config (NmVpnSsoService *self)
{
    NmVpnSsoServicePrivate *priv = self->priv;
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

    /* Tunnel device is required - default to tun0 if not detected */
    const gchar *tundev = priv->tundev ? priv->tundev : "tun0";
    g_variant_builder_add (&builder, "{sv}", "tundev",
                           g_variant_new_string (tundev));
    g_message ("Reporting tunnel device to NetworkManager: %s", tundev);

    /* If we have an IP address, include it (convert to uint32) */
    if (priv->ip4_address) {
        struct in_addr addr;
        if (inet_pton (AF_INET, priv->ip4_address, &addr) == 1) {
            g_variant_builder_add (&builder, "{sv}", "address",
                                   g_variant_new_uint32 (addr.s_addr));
            /* Default prefix length for VPN - typically /32 or determined by server */
            g_variant_builder_add (&builder, "{sv}", "prefix",
                                   g_variant_new_uint32 (32));
        }
    }

    /* Include the VPN gateway IP - use ip4_gateway (parsed from OpenConnect output)
     * which contains the actual IP address, not the hostname in priv->gateway */
    if (priv->ip4_gateway) {
        struct in_addr addr;
        if (inet_pton (AF_INET, priv->ip4_gateway, &addr) == 1) {
            g_variant_builder_add (&builder, "{sv}", "gateway",
                                   g_variant_new_uint32 (addr.s_addr));
            g_message ("Reporting gateway IP to NetworkManager: %s", priv->ip4_gateway);
        } else {
            g_warning ("Failed to convert gateway IP '%s' to network format", priv->ip4_gateway);
        }
    } else {
        g_warning ("No gateway IP detected from OpenConnect output");
    }

    /* Include DNS servers from VPN */
    if (priv->ip4_dns && priv->ip4_dns->len > 0) {
        GVariantBuilder dns_builder;
        g_variant_builder_init (&dns_builder, G_VARIANT_TYPE ("au"));
        for (guint i = 0; i < priv->ip4_dns->len; i++) {
            const gchar *dns_str = g_ptr_array_index (priv->ip4_dns, i);
            struct in_addr addr;
            if (inet_pton (AF_INET, dns_str, &addr) == 1) {
                g_variant_builder_add (&dns_builder, "u", addr.s_addr);
                g_message ("Reporting DNS server to NetworkManager: %s", dns_str);
            }
        }
        g_variant_builder_add (&builder, "{sv}", "dns",
                               g_variant_builder_end (&dns_builder));
    } else {
        g_warning ("No DNS servers detected from OpenConnect output");
    }

    GVariant *config = g_variant_builder_end (&builder);
    nm_vpn_service_plugin_set_ip4_config (NM_VPN_SERVICE_PLUGIN (self), config);
    g_message ("IP4 configuration reported to NetworkManager");
}

/*
 * Check if a network device exists in /sys/class/net/
 */
static gboolean
tun_device_exists (const gchar *devname)
{
    g_autofree gchar *path = g_strdup_printf ("/sys/class/net/%s", devname);
    return g_file_test (path, G_FILE_TEST_IS_DIR);
}

/*
 * Retry callback for IP4 config reporting - waits for tun device to exist
 */
static gboolean
report_ip4_config_retry_cb (gpointer user_data)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (user_data);
    NmVpnSsoServicePrivate *priv = self->priv;
    const gchar *tundev = priv->tundev ? priv->tundev : "tun0";

    priv->ip4_config_retry_count++;

    if (tun_device_exists (tundev)) {
        g_message ("Tunnel device %s now exists (attempt %d), reporting IP4 config",
                   tundev, priv->ip4_config_retry_count);
        priv->ip4_config_retry_source = 0;
        report_ip4_config (self);
        return G_SOURCE_REMOVE;
    }

    /* Max 50 retries (5 seconds total with 100ms interval) */
    if (priv->ip4_config_retry_count >= 50) {
        g_warning ("Tunnel device %s did not appear after 5 seconds, reporting anyway",
                   tundev);
        priv->ip4_config_retry_source = 0;
        report_ip4_config (self);
        return G_SOURCE_REMOVE;
    }

    g_debug ("Waiting for tunnel device %s (attempt %d)...",
             tundev, priv->ip4_config_retry_count);
    return G_SOURCE_CONTINUE;
}

/*
 * Schedule IP4 config reporting - waits for tun device to appear
 */
static void
schedule_ip4_config_report (NmVpnSsoService *self)
{
    NmVpnSsoServicePrivate *priv = self->priv;
    const gchar *tundev = priv->tundev ? priv->tundev : "tun0";

    /* Cancel any pending retry */
    if (priv->ip4_config_retry_source > 0) {
        g_source_remove (priv->ip4_config_retry_source);
        priv->ip4_config_retry_source = 0;
    }

    /* Check if device already exists */
    if (tun_device_exists (tundev)) {
        g_message ("Tunnel device %s already exists, reporting IP4 config immediately",
                   tundev);
        report_ip4_config (self);
        return;
    }

    /* Device doesn't exist yet, schedule retries every 100ms */
    g_message ("Tunnel device %s not yet created, waiting...", tundev);
    priv->ip4_config_retry_count = 0;
    priv->ip4_config_retry_source = g_timeout_add (100, report_ip4_config_retry_cb, self);
}

static gboolean
openconnect_stdout_cb (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (user_data);
    NmVpnSsoServicePrivate *priv = self->priv;
    gchar buf[1024];
    gsize bytes_read;
    GIOStatus status;

    if (condition & G_IO_IN) {
        status = g_io_channel_read_chars (source, buf, sizeof (buf) - 1, &bytes_read, NULL);
        if (status == G_IO_STATUS_NORMAL && bytes_read > 0) {
            buf[bytes_read] = '\0';
            g_message ("OpenConnect: %s", buf);

            /* Try to parse tunnel device and IP from output */
            parse_openconnect_output (self, buf);

            /* Parse for connection success indicators.
             * IMPORTANT: "Connected to X.X.X.X" is just the TCP connection - too early!
             * We need to wait for "Configured as X.X.X.X" which means the tunnel is up.
             * Even then, the tun device may not exist immediately - schedule with retries.
             */
            if (strstr (buf, "Configured as") != NULL) {
                if (priv->state != VPN_STATE_CONNECTED) {
                    priv->state = VPN_STATE_CONNECTED;

                    /* Schedule IP4 configuration report - waits for tun device */
                    schedule_ip4_config_report (self);
                }
            }
        }
    }

    if (condition & G_IO_HUP) {
        return FALSE;
    }

    return TRUE;
}

static gboolean
openconnect_stderr_cb (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (user_data);
    NmVpnSsoServicePrivate *priv = self->priv;
    gchar buf[1024];
    gsize bytes_read;
    GIOStatus status;

    if (condition & G_IO_IN) {
        status = g_io_channel_read_chars (source, buf, sizeof (buf) - 1, &bytes_read, NULL);
        if (status == G_IO_STATUS_NORMAL && bytes_read > 0) {
            buf[bytes_read] = '\0';
            g_message ("OpenConnect stderr: %s", buf);

            /* Also parse stderr - openconnect often outputs important info there */
            parse_openconnect_output (self, buf);

            /* Check for connection success in stderr too.
             * IMPORTANT: Wait for "Configured as" - the tun device only exists then.
             */
            if (strstr (buf, "Configured as") != NULL) {
                if (priv->state != VPN_STATE_CONNECTED) {
                    priv->state = VPN_STATE_CONNECTED;
                    schedule_ip4_config_report (self);
                }
            }
        }
    }

    if (condition & G_IO_HUP) {
        return FALSE;
    }

    return TRUE;
}

static void
openconnect_child_watch_cb (GPid pid, gint status, gpointer user_data)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (user_data);
    NmVpnSsoServicePrivate *priv = self->priv;

    g_message ("OpenConnect process exited with status %d", status);

    /* Clean up OpenConnect process resources */
    if (priv->openconnect_stdout_watch) {
        g_source_remove (priv->openconnect_stdout_watch);
        priv->openconnect_stdout_watch = 0;
    }
    if (priv->openconnect_stderr_watch) {
        g_source_remove (priv->openconnect_stderr_watch);
        priv->openconnect_stderr_watch = 0;
    }
    if (priv->openconnect_watch_timer) {
        g_source_remove (priv->openconnect_watch_timer);
        priv->openconnect_watch_timer = 0;
    }
    if (priv->openconnect_stdout) {
        g_io_channel_unref (priv->openconnect_stdout);
        priv->openconnect_stdout = NULL;
    }
    if (priv->openconnect_stderr) {
        g_io_channel_unref (priv->openconnect_stderr);
        priv->openconnect_stderr = NULL;
    }
    if (priv->openconnect_stdin) {
        g_io_channel_unref (priv->openconnect_stdin);
        priv->openconnect_stdin = NULL;
    }

    g_spawn_close_pid (pid);
    priv->openconnect_pid = 0;
    priv->openconnect_child_watch = 0;

    /* Handle connection failure */
    if (priv->state == VPN_STATE_CONNECTED || priv->state == VPN_STATE_CONNECTING) {
        if (WIFEXITED (status) && WEXITSTATUS (status) != 0) {
            gint exit_code = WEXITSTATUS (status);
            g_warning ("OpenConnect failed with exit code %d", exit_code);

            /* OpenConnect failed - check if we should fallback to SSO */
            if (priv->using_cached_credentials) {
                g_message ("Cached credentials failed (exit code %d) - clearing cache and falling back to SSO", exit_code);

                /* Clear invalid cached credentials */
                vpn_sso_credential_cache_clear_async (priv->gateway, priv->protocol,
                                                      NULL, NULL, NULL);

                /* Clear credential state */
                g_clear_pointer (&priv->sso_cookie, g_free);
                g_clear_pointer (&priv->sso_fingerprint, g_free);
                priv->using_cached_credentials = FALSE;

                /* Fallback to SSO authentication */
                priv->state = VPN_STATE_AUTHENTICATING;
                start_sso_authentication (self);
                return;
            }

            nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (self),
                                          NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
        } else {
            nm_vpn_service_plugin_disconnect (NM_VPN_SERVICE_PLUGIN (self), NULL);
        }
    }

    cleanup_connection (self);
}

static void
start_openconnect (NmVpnSsoService *self)
{
    NmVpnSsoServicePrivate *priv = self->priv;
    GError *error = NULL;
    GPtrArray *argv;
    gchar **envp;
    gint stdin_fd, stdout_fd, stderr_fd;

    g_message ("Starting OpenConnect for gateway: %s (protocol: %s)", priv->gateway, priv->protocol);
    g_message ("  cookie: %s (len=%zu)",
               priv->sso_cookie ? "(present)" : "(null)",
               priv->sso_cookie ? strlen (priv->sso_cookie) : 0);
    g_message ("  fingerprint: %s",
               priv->sso_fingerprint ? priv->sso_fingerprint : "(null)");
    g_message ("  using_cached: %s", priv->using_cached_credentials ? "YES" : "NO");

    argv = g_ptr_array_new ();

    g_ptr_array_add (argv, (gpointer) "openconnect");

    /* Protocol-specific arguments */
    if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_GP) == 0) {
        g_ptr_array_add (argv, (gpointer) "--protocol=gp");
        g_ptr_array_add (argv, (gpointer) "--useragent=PAN GlobalProtect");
        g_ptr_array_add (argv, (gpointer) "--os=linux-64");

        if (priv->username) {
            g_ptr_array_add (argv, (gpointer) "--user");
            g_ptr_array_add (argv, (gpointer) priv->username);
        }

        if (priv->sso_cookie) {
            /* Use cached usergroup if available (e.g., portal:portal-userauthcookie),
             * otherwise default to portal:prelogin-cookie for initial SSO auth.
             */
            if (priv->usergroup && *priv->usergroup) {
                gchar *usergroup_arg = g_strdup_printf ("--usergroup=%s", priv->usergroup);
                g_ptr_array_add (argv, (gpointer) usergroup_arg);
                g_message ("Using usergroup: %s", priv->usergroup);
            } else {
                g_ptr_array_add (argv, (gpointer) "--usergroup=portal:prelogin-cookie");
                g_message ("Using default usergroup: portal:prelogin-cookie");
            }
            g_ptr_array_add (argv, (gpointer) "--passwd-on-stdin");
        }
    } else if (g_strcmp0 (priv->protocol, NM_VPN_SSO_PROTOCOL_AC) == 0) {
        /* AnyConnect: use credentials from openconnect-sso --authenticate */
        g_ptr_array_add (argv, (gpointer) "--protocol=anyconnect");

        if (priv->username) {
            g_ptr_array_add (argv, (gpointer) "--user");
            g_ptr_array_add (argv, (gpointer) priv->username);
        }

        /* Server certificate fingerprint - required to prevent MITM warnings */
        if (priv->sso_fingerprint) {
            gchar *servercert_arg = g_strdup_printf ("--servercert=%s", priv->sso_fingerprint);
            g_ptr_array_add (argv, (gpointer) servercert_arg);
        }

        /* Cookie authentication - send cookie via stdin */
        if (priv->sso_cookie) {
            g_ptr_array_add (argv, (gpointer) "--cookie-on-stdin");
        }
    }

    /* Additional arguments */
    if (priv->extra_args) {
        gchar **extra_argv = g_strsplit (priv->extra_args, " ", -1);
        for (gchar **arg = extra_argv; *arg; arg++) {
            if (strlen (*arg) > 0) {
                g_ptr_array_add (argv, (gpointer) *arg);
            }
        }
    }

    /* Non-interactive mode */
    g_ptr_array_add (argv, (gpointer) "--non-inter");

    /* Gateway (must be last positional argument) */
    g_ptr_array_add (argv, (gpointer) priv->gateway);
    g_ptr_array_add (argv, NULL);

    /* Log the command line for debugging */
    {
        g_autofree gchar *cmdline = g_strjoinv (" ", (gchar **) argv->pdata);
        g_message ("OpenConnect command: %s", cmdline);
    }

    /* Build environment - openconnect typically runs as root so doesn't
     * strictly need display env, but we build it anyway for consistency */
    envp = build_subprocess_environment (NULL);

    if (!g_spawn_async_with_pipes (NULL, /* working_directory */
                                   (gchar **) argv->pdata,
                                   envp,
                                   G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                   NULL, /* child_setup */
                                   NULL, /* user_data */
                                   &priv->openconnect_pid,
                                   &stdin_fd,
                                   &stdout_fd,
                                   &stderr_fd,
                                   &error)) {
        g_warning ("Failed to spawn OpenConnect: %s", error->message);
        nm_vpn_service_plugin_failure (NM_VPN_SERVICE_PLUGIN (self),
                                      NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
        g_error_free (error);
        g_ptr_array_free (argv, TRUE);
        g_strfreev (envp);
        return;
    }

    g_strfreev (envp);
    g_message ("OpenConnect started with PID %d", priv->openconnect_pid);

    /* Set up I/O channels */
    priv->openconnect_stdin = g_io_channel_unix_new (stdin_fd);
    priv->openconnect_stdout = g_io_channel_unix_new (stdout_fd);
    priv->openconnect_stderr = g_io_channel_unix_new (stderr_fd);

    g_io_channel_set_encoding (priv->openconnect_stdin, NULL, NULL);
    g_io_channel_set_encoding (priv->openconnect_stdout, NULL, NULL);
    g_io_channel_set_encoding (priv->openconnect_stderr, NULL, NULL);
    g_io_channel_set_buffered (priv->openconnect_stdin, FALSE);
    g_io_channel_set_buffered (priv->openconnect_stdout, FALSE);
    g_io_channel_set_buffered (priv->openconnect_stderr, FALSE);

    /* If we have an SSO cookie, send it to stdin */
    if (priv->sso_cookie) {
        gsize bytes_written;
        GError *write_error = NULL;
        gsize cookie_len = strlen (priv->sso_cookie);

        g_message ("Writing cookie to OpenConnect stdin (length=%zu, first 20 chars: %.20s...)",
                   cookie_len, priv->sso_cookie);

        g_io_channel_write_chars (priv->openconnect_stdin,
                                 priv->sso_cookie,
                                 -1,
                                 &bytes_written,
                                 &write_error);
        if (write_error) {
            g_warning ("Failed to write cookie to OpenConnect: %s", write_error->message);
            g_error_free (write_error);
        } else {
            g_io_channel_write_chars (priv->openconnect_stdin, "\n", -1, &bytes_written, NULL);
            g_io_channel_flush (priv->openconnect_stdin, NULL);
            g_message ("Cookie written to OpenConnect stdin successfully (%zu bytes)", cookie_len);

            /* Close stdin to signal EOF - openconnect expects this */
            g_io_channel_shutdown (priv->openconnect_stdin, TRUE, NULL);
            g_io_channel_unref (priv->openconnect_stdin);
            priv->openconnect_stdin = NULL;
        }
    }

    priv->openconnect_stdout_watch = g_io_add_watch (priv->openconnect_stdout,
                                                    G_IO_IN | G_IO_HUP,
                                                    openconnect_stdout_cb,
                                                    self);
    priv->openconnect_stderr_watch = g_io_add_watch (priv->openconnect_stderr,
                                                    G_IO_IN | G_IO_HUP,
                                                    openconnect_stderr_cb,
                                                    self);

    priv->openconnect_child_watch = g_child_watch_add (priv->openconnect_pid,
                                                      openconnect_child_watch_cb,
                                                      self);

    g_ptr_array_free (argv, TRUE);
}

/*
 * Connection Management
 */

static void
cleanup_connection (NmVpnSsoService *self)
{
    NmVpnSsoServicePrivate *priv = self->priv;

    g_message ("Cleaning up connection resources");

    /* Kill SSO process if running */
    if (priv->sso_pid) {
        kill (priv->sso_pid, SIGTERM);
    }

    /* Disconnect OpenConnect process if running.
     *
     * IMPORTANT: We use SIGHUP instead of SIGTERM!
     * - SIGTERM causes openconnect to log off the session, invalidating the cookie
     * - SIGHUP disconnects but does NOT log off, preserving the session cookie
     *   for reconnection (see openconnect(8) man page SIGNALS section)
     *
     * This allows the cached credentials to be reused for reconnection within
     * the server's session timeout (typically 8-12 hours for AnyConnect).
     */
    if (priv->openconnect_pid) {
        g_message ("Sending SIGHUP to openconnect (PID %d) to preserve session cookie",
                   priv->openconnect_pid);
        kill (priv->openconnect_pid, SIGHUP);
    }

    /* Clean up SSO resources */
    if (priv->sso_stdout_watch) {
        g_source_remove (priv->sso_stdout_watch);
        priv->sso_stdout_watch = 0;
    }
    if (priv->sso_stderr_watch) {
        g_source_remove (priv->sso_stderr_watch);
        priv->sso_stderr_watch = 0;
    }
    if (priv->sso_child_watch) {
        g_source_remove (priv->sso_child_watch);
        priv->sso_child_watch = 0;
    }
    if (priv->sso_stdout) {
        g_io_channel_unref (priv->sso_stdout);
        priv->sso_stdout = NULL;
    }
    if (priv->sso_stderr) {
        g_io_channel_unref (priv->sso_stderr);
        priv->sso_stderr = NULL;
    }
    if (priv->sso_output) {
        g_string_free (priv->sso_output, TRUE);
        priv->sso_output = NULL;
    }

    /* Clean up OpenConnect resources */
    if (priv->openconnect_stdout_watch) {
        g_source_remove (priv->openconnect_stdout_watch);
        priv->openconnect_stdout_watch = 0;
    }
    if (priv->openconnect_stderr_watch) {
        g_source_remove (priv->openconnect_stderr_watch);
        priv->openconnect_stderr_watch = 0;
    }
    if (priv->openconnect_child_watch) {
        g_source_remove (priv->openconnect_child_watch);
        priv->openconnect_child_watch = 0;
    }
    if (priv->openconnect_watch_timer) {
        g_source_remove (priv->openconnect_watch_timer);
        priv->openconnect_watch_timer = 0;
    }
    if (priv->ip4_config_retry_source) {
        g_source_remove (priv->ip4_config_retry_source);
        priv->ip4_config_retry_source = 0;
    }
    if (priv->openconnect_stdout) {
        g_io_channel_unref (priv->openconnect_stdout);
        priv->openconnect_stdout = NULL;
    }
    if (priv->openconnect_stderr) {
        g_io_channel_unref (priv->openconnect_stderr);
        priv->openconnect_stderr = NULL;
    }
    if (priv->openconnect_stdin) {
        g_io_channel_unref (priv->openconnect_stdin);
        priv->openconnect_stdin = NULL;
    }

    /* Clean up configuration */
    g_clear_pointer (&priv->sso_cookie, g_free);
    g_clear_pointer (&priv->sso_fingerprint, g_free);
    g_clear_pointer (&priv->tundev, g_free);
    g_clear_pointer (&priv->ip4_address, g_free);
    g_clear_pointer (&priv->ip4_netmask, g_free);
    g_clear_pointer (&priv->ip4_gateway, g_free);

    if (priv->ip4_dns) {
        g_ptr_array_free (priv->ip4_dns, TRUE);
        priv->ip4_dns = NULL;
    }
    if (priv->ip4_routes) {
        g_ptr_array_free (priv->ip4_routes, TRUE);
        priv->ip4_routes = NULL;
    }

    priv->state = VPN_STATE_IDLE;
}

static gboolean
connect_to_vpn (NmVpnSsoService *self, GError **error)
{
    NmVpnSsoServicePrivate *priv = self->priv;

    g_message ("Initiating VPN connection to %s using protocol %s",
               priv->gateway, priv->protocol);

    /* Validate configuration */
    if (!priv->gateway || strlen (priv->gateway) == 0) {
        g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
                    "Gateway not specified");
        return FALSE;
    }

    if (!priv->protocol || strlen (priv->protocol) == 0) {
        g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
                    "Protocol not specified");
        return FALSE;
    }

    /* Check for cached credentials before starting SSO authentication.
     * If valid cached credentials exist, we can skip the browser-based
     * SSO flow and connect directly. The callback will either use cached
     * credentials or fall back to SSO authentication. */
    g_message ("Checking for cached credentials...");
    vpn_sso_credential_cache_lookup_async (priv->gateway,
                                           priv->protocol,
                                           NULL, /* cancellable */
                                           credential_lookup_cb,
                                           self);

    return TRUE;
}

/*
 * NMVpnServicePlugin virtual method implementations
 */

static gboolean
real_connect (NMVpnServicePlugin *plugin,
              NMConnection *connection,
              GError **error)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (plugin);
    NmVpnSsoServicePrivate *priv = self->priv;
    NMSettingVpn *s_vpn;
    const char *value;

    g_message ("VPN connect requested");

    s_vpn = nm_connection_get_setting_vpn (connection);
    if (!s_vpn) {
        g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
                    "Missing VPN setting");
        return FALSE;
    }

    /* Extract connection settings */
    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_GATEWAY);
    if (value)
        priv->gateway = g_strdup (value);

    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_PROTOCOL);
    if (value)
        priv->protocol = g_strdup (value);

    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_USERNAME);
    if (value)
        priv->username = g_strdup (value);

    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_USERGROUP);
    if (value)
        priv->usergroup = g_strdup (value);

    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_EXTRA_ARGS);
    if (value)
        priv->extra_args = g_strdup (value);

    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_CACHE_HOURS);
    if (value)
        priv->cache_hours = atoi (value);

    value = nm_setting_vpn_get_data_item (s_vpn, NM_VPN_SSO_KEY_EXTERNAL_BROWSER);
    priv->external_browser = (value && g_strcmp0 (value, "yes") == 0);
    g_message ("External browser SSO: %s", priv->external_browser ? "enabled" : "disabled");

    return connect_to_vpn (self, error);
}

static gboolean
real_need_secrets (NMVpnServicePlugin *plugin,
                   NMConnection *connection,
                   const char **setting_name,
                   GError **error)
{
    /* We handle authentication via SSO, so no secrets needed from NM */
    return FALSE;
}

static gboolean
real_disconnect (NMVpnServicePlugin *plugin,
                GError **error)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (plugin);

    g_message ("VPN disconnect requested");

    cleanup_connection (self);

    return TRUE;
}

/*
 * Object lifecycle
 */

static void
nm_vpn_sso_service_init (NmVpnSsoService *self)
{
    self->priv = nm_vpn_sso_service_get_instance_private (self);
    self->priv->state = VPN_STATE_IDLE;

    g_message ("VPN SSO service initialized");
}

static void
dispose (GObject *object)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (object);

    cleanup_connection (self);

    G_OBJECT_CLASS (nm_vpn_sso_service_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    NmVpnSsoService *self = NM_VPN_SSO_SERVICE (object);
    NmVpnSsoServicePrivate *priv = self->priv;

    g_free (priv->gateway);
    g_free (priv->protocol);
    g_free (priv->username);
    g_free (priv->usergroup);
    g_free (priv->extra_args);

    g_message ("VPN SSO service finalized");

    G_OBJECT_CLASS (nm_vpn_sso_service_parent_class)->finalize (object);
}

static void
nm_vpn_sso_service_class_init (NmVpnSsoServiceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    NMVpnServicePluginClass *plugin_class = NM_VPN_SERVICE_PLUGIN_CLASS (klass);

    /* Virtual methods */
    object_class->dispose = dispose;
    object_class->finalize = finalize;

    plugin_class->connect = real_connect;
    plugin_class->need_secrets = real_need_secrets;
    plugin_class->disconnect = real_disconnect;

    g_message ("VPN SSO service class initialized");
}

NmVpnSsoService *
nm_vpn_sso_service_new (const char *bus_name)
{
    NmVpnSsoService *service;
    GError *error = NULL;

    service = g_initable_new (NM_TYPE_VPN_SSO_SERVICE,
                             NULL,
                             &error,
                             NM_VPN_SERVICE_PLUGIN_DBUS_SERVICE_NAME, bus_name,
                             NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, TRUE,
                             NULL);

    if (!service) {
        g_warning ("Failed to create VPN service: %s", error ? error->message : "unknown error");
        g_clear_error (&error);
        return NULL;
    }

    return service;
}
