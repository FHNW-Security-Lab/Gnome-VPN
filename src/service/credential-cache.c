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
#include "credential-cache.h"
#include "utils.h"

#include <string.h>
#include <pwd.h>
#include <sys/types.h>

/**
 * SECTION:credential-cache
 * @title: Credential Cache
 * @short_description: GNOME Keyring storage for VPN SSO credentials
 *
 * This module provides secure storage for VPN SSO credentials using
 * the secret-tool command to access GNOME Keyring.
 *
 * When running as root, we spawn secret-tool as the target user using
 * runuser, since D-Bus session buses reject connections from different UIDs.
 */

/* Cache the target username for keyring operations */
static gchar *cached_target_username = NULL;

/*
 * Get the username for keyring operations.
 * When running as root, we need to find the logged-in user's username.
 */
static const gchar *
get_target_username (void)
{
    if (cached_target_username)
        return cached_target_username;

    /* If not running as root, use current user */
    if (getuid () != 0) {
        struct passwd *pw = getpwuid (getuid ());
        if (pw) {
            cached_target_username = g_strdup (pw->pw_name);
            g_message ("KEYRING: Using current user: %s", cached_target_username);
            return cached_target_username;
        }
    }

    /* Running as root - find graphical session user */
    VpnSsoSessionEnv *session_env = vpn_sso_get_graphical_session_env ();
    if (session_env && session_env->uid > 0) {
        struct passwd *pw = getpwuid (session_env->uid);
        if (pw) {
            cached_target_username = g_strdup (pw->pw_name);
            g_message ("KEYRING: Using graphical session user: %s (UID %d)",
                       cached_target_username, session_env->uid);
        }
        vpn_sso_session_env_free (session_env);
    }

    if (!cached_target_username) {
        g_warning ("KEYRING: Could not determine target username for keyring access");
    }

    return cached_target_username;
}

void
vpn_sso_cached_credential_free (VpnSsoCachedCredential *credential)
{
    if (!credential)
        return;

    g_free (credential->gateway);
    g_free (credential->protocol);
    g_free (credential->username);

    /* Securely clear the cookie before freeing */
    if (credential->cookie) {
        memset (credential->cookie, 0, strlen (credential->cookie));
        g_free (credential->cookie);
    }

    g_free (credential->fingerprint);
    g_free (credential->usergroup);
    g_free (credential);
}

/*
 * Serialize credential data to JSON for storage in keyring
 */
static gchar *
serialize_credential (const gchar *gateway,
                      const gchar *protocol,
                      const gchar *username,
                      const gchar *cookie,
                      const gchar *fingerprint,
                      const gchar *usergroup,
                      gint64       created_at,
                      gint64       expires_at)
{
    g_autoptr(GString) json = g_string_new ("{\n");

    g_string_append_printf (json, "  \"gateway\": \"%s\",\n", gateway);
    g_string_append_printf (json, "  \"protocol\": \"%s\",\n", protocol);
    g_string_append_printf (json, "  \"created_at\": %" G_GINT64_FORMAT ",\n", created_at);
    g_string_append_printf (json, "  \"expires_at\": %" G_GINT64_FORMAT, expires_at);

    if (username) {
        g_autofree gchar *escaped = g_strescape (username, NULL);
        g_string_append_printf (json, ",\n  \"username\": \"%s\"", escaped);
    }

    if (cookie) {
        g_autofree gchar *escaped = g_strescape (cookie, NULL);
        g_string_append_printf (json, ",\n  \"cookie\": \"%s\"", escaped);
    }

    if (fingerprint) {
        g_autofree gchar *escaped = g_strescape (fingerprint, NULL);
        g_string_append_printf (json, ",\n  \"fingerprint\": \"%s\"", escaped);
    }

    if (usergroup) {
        g_autofree gchar *escaped = g_strescape (usergroup, NULL);
        g_string_append_printf (json, ",\n  \"usergroup\": \"%s\"", escaped);
    }

    g_string_append (json, "\n}");

    return g_string_free (g_steal_pointer (&json), FALSE);
}

/*
 * Parse a JSON string field
 */
static gchar *
parse_json_string (const gchar *json, const gchar *key)
{
    g_autofree gchar *pattern = g_strdup_printf ("\"%s\"\\s*:\\s*\"", key);
    g_autoptr(GRegex) regex = g_regex_new (pattern, 0, 0, NULL);
    g_autoptr(GMatchInfo) match_info = NULL;

    if (!g_regex_match (regex, json, 0, &match_info))
        return NULL;

    gint end;
    g_match_info_fetch_pos (match_info, 0, NULL, &end);

    /* Find the closing quote */
    const gchar *value_start = json + end;
    const gchar *p = value_start;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1))
            p++;
        p++;
    }

    if (*p != '"')
        return NULL;

    gchar *escaped = g_strndup (value_start, p - value_start);
    gchar *result = g_strcompress (escaped);
    g_free (escaped);

    return result;
}

/*
 * Parse a JSON integer field
 */
static gint64
parse_json_int64 (const gchar *json, const gchar *key)
{
    g_autofree gchar *pattern = g_strdup_printf ("\"%s\"\\s*:\\s*([0-9]+)", key);
    g_autoptr(GRegex) regex = g_regex_new (pattern, 0, 0, NULL);
    g_autoptr(GMatchInfo) match_info = NULL;

    if (!g_regex_match (regex, json, 0, &match_info))
        return 0;

    g_autofree gchar *value = g_match_info_fetch (match_info, 1);
    return g_ascii_strtoll (value, NULL, 10);
}

/*
 * Deserialize credential data from JSON
 */
static VpnSsoCachedCredential *
deserialize_credential (const gchar *json)
{
    if (!json || !*json)
        return NULL;

    VpnSsoCachedCredential *cred = g_new0 (VpnSsoCachedCredential, 1);

    cred->gateway = parse_json_string (json, "gateway");
    cred->protocol = parse_json_string (json, "protocol");
    cred->username = parse_json_string (json, "username");
    cred->cookie = parse_json_string (json, "cookie");
    cred->fingerprint = parse_json_string (json, "fingerprint");
    cred->usergroup = parse_json_string (json, "usergroup");
    cred->created_at = parse_json_int64 (json, "created_at");
    cred->expires_at = parse_json_int64 (json, "expires_at");

    return cred;
}

/*
 * Run secret-tool as target user.
 * Returns stdout content on success, NULL on failure.
 */
static gchar *
run_secret_tool (const gchar * const *argv,
                 const gchar         *stdin_data,
                 GError             **error)
{
    const gchar *username = get_target_username ();
    if (!username) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Could not determine target user for keyring access");
        return NULL;
    }

    /* Build command: runuser -u <username> -- secret-tool <args...> */
    g_autoptr(GPtrArray) cmd = g_ptr_array_new ();

    /* If running as root, use runuser to switch to target user */
    if (getuid () == 0) {
        g_ptr_array_add (cmd, (gchar *) "/usr/sbin/runuser");
        g_ptr_array_add (cmd, (gchar *) "-u");
        g_ptr_array_add (cmd, (gchar *) username);
        g_ptr_array_add (cmd, (gchar *) "--");
    }

    /* Add secret-tool command and arguments */
    for (int i = 0; argv[i] != NULL; i++) {
        g_ptr_array_add (cmd, (gchar *) argv[i]);
    }
    g_ptr_array_add (cmd, NULL);

    g_autofree gchar *cmdline = g_strjoinv (" ", (gchar **) cmd->pdata);
    g_message ("KEYRING: Running command: %s", cmdline);

    GSubprocessFlags flags = G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE;
    if (stdin_data)
        flags |= G_SUBPROCESS_FLAGS_STDIN_PIPE;

    g_autoptr(GSubprocess) proc = g_subprocess_newv ((const gchar * const *) cmd->pdata,
                                                      flags, error);
    if (!proc) {
        g_prefix_error (error, "Failed to spawn secret-tool: ");
        return NULL;
    }

    g_autofree gchar *stdout_data = NULL;
    g_autofree gchar *stderr_data = NULL;
    GBytes *stdin_bytes = stdin_data ? g_bytes_new_static (stdin_data, strlen (stdin_data)) : NULL;

    gboolean success = g_subprocess_communicate_utf8 (proc,
                                                       stdin_data,
                                                       NULL, /* cancellable */
                                                       &stdout_data,
                                                       &stderr_data,
                                                       error);

    if (stdin_bytes)
        g_bytes_unref (stdin_bytes);

    if (!success) {
        g_prefix_error (error, "secret-tool communication failed: ");
        return NULL;
    }

    gint exit_status = g_subprocess_get_exit_status (proc);
    if (exit_status != 0) {
        /* Exit status 1 from secret-tool lookup means "not found" - not an error */
        if (exit_status == 1 && g_strstr_len (cmdline, -1, "lookup")) {
            g_message ("KEYRING: secret-tool lookup returned no results");
            return NULL;
        }

        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "secret-tool exited with status %d: %s",
                     exit_status, stderr_data ? stderr_data : "(no error output)");
        return NULL;
    }

    return g_steal_pointer (&stdout_data);
}

/*
 * Data structure for async store operation
 */
typedef struct {
    gchar *gateway;
    gchar *protocol;
    gchar *username;
    gchar *json;
    gchar *label;
} StoreData;

static void
store_data_free (StoreData *data)
{
    if (data) {
        g_free (data->gateway);
        g_free (data->protocol);
        g_free (data->username);
        g_free (data->label);
        if (data->json) {
            memset (data->json, 0, strlen (data->json));
            g_free (data->json);
        }
        g_free (data);
    }
}

/*
 * Thread function for storing credentials using secret-tool.
 */
static void
store_thread_func (GTask        *task,
                   gpointer      source_object G_GNUC_UNUSED,
                   gpointer      task_data,
                   GCancellable *cancellable G_GNUC_UNUSED)
{
    StoreData *data = task_data;
    GError *error = NULL;

    g_message ("KEYRING STORE THREAD: Storing credentials for %s:%s",
               data->gateway, data->protocol);

    /* Build secret-tool store command */
    const gchar *argv[] = {
        "/usr/bin/secret-tool", "store",
        "--label", data->label,
        "xdg:schema", "org.freedesktop.NetworkManager.vpn-sso",
        "gateway", data->gateway,
        "protocol", data->protocol,
        NULL
    };

    /* secret-tool reads the secret from stdin */
    gchar *result = run_secret_tool (argv, data->json, &error);

    if (error) {
        g_warning ("KEYRING STORE THREAD: FAILED to store credentials: %s", error->message);
        g_task_return_error (task, error);
    } else {
        g_message ("KEYRING STORE THREAD: SUCCESS - credentials stored for %s:%s",
                   data->gateway, data->protocol);
        g_free (result);
        g_task_return_boolean (task, TRUE);
    }
}

/**
 * vpn_sso_credential_cache_store_async:
 *
 * Stores VPN SSO credentials in GNOME Keyring.
 */
void
vpn_sso_credential_cache_store_async (const gchar         *gateway,
                                       const gchar         *protocol,
                                       const gchar         *username,
                                       const gchar         *cookie,
                                       const gchar         *fingerprint,
                                       const gchar         *usergroup,
                                       gint                 cache_hours,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
    GTask *task;
    StoreData *data;

    g_return_if_fail (gateway != NULL);
    g_return_if_fail (protocol != NULL);

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_credential_cache_store_async);

    if (cache_hours <= 0)
        cache_hours = VPN_SSO_DEFAULT_CACHE_DURATION_HOURS;

    /* Calculate timestamps */
    gint64 now = g_get_real_time () / G_USEC_PER_SEC;
    gint64 expires_at = now + (cache_hours * 3600);

    g_message ("KEYRING STORE: gateway=%s protocol=%s username=%s cookie=%s (expires in %d hours)",
               gateway, protocol, username ? username : "(null)",
               cookie ? "(present)" : "(null)", cache_hours);

    /* Serialize to JSON */
    gchar *json = serialize_credential (gateway, protocol, username,
                                        cookie, fingerprint, usergroup,
                                        now, expires_at);

    /* Store data for thread */
    data = g_new0 (StoreData, 1);
    data->gateway = g_strdup (gateway);
    data->protocol = g_strdup (protocol);
    data->username = g_strdup (username);
    data->json = json;
    data->label = g_strdup_printf ("VPN SSO: %s (%s)", gateway, protocol);
    g_task_set_task_data (task, data, (GDestroyNotify) store_data_free);

    /* Run in thread */
    g_task_run_in_thread (task, store_thread_func);
    g_object_unref (task);
}

/**
 * vpn_sso_credential_cache_store_finish:
 *
 * Completes credential storage.
 */
gboolean
vpn_sso_credential_cache_store_finish (GAsyncResult  *result,
                                        GError       **error)
{
    g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
}

/*
 * Data structure for async lookup operation
 */
typedef struct {
    gchar *gateway;
    gchar *protocol;
} LookupData;

static void
lookup_data_free (LookupData *data)
{
    if (data) {
        g_free (data->gateway);
        g_free (data->protocol);
        g_free (data);
    }
}

/*
 * Thread function for looking up credentials using secret-tool.
 */
static void
lookup_thread_func (GTask        *task,
                    gpointer      source_object G_GNUC_UNUSED,
                    gpointer      task_data,
                    GCancellable *cancellable G_GNUC_UNUSED)
{
    LookupData *data = task_data;
    GError *error = NULL;

    g_message ("KEYRING LOOKUP THREAD: Looking up credentials for %s:%s",
               data->gateway, data->protocol);

    /* Build secret-tool lookup command */
    const gchar *argv[] = {
        "/usr/bin/secret-tool", "lookup",
        "xdg:schema", "org.freedesktop.NetworkManager.vpn-sso",
        "gateway", data->gateway,
        "protocol", data->protocol,
        NULL
    };

    gchar *secret = run_secret_tool (argv, NULL, &error);

    if (error) {
        g_warning ("KEYRING LOOKUP THREAD: Error looking up credentials: %s", error->message);
        g_task_return_error (task, error);
        return;
    }

    if (!secret || !*secret) {
        g_message ("KEYRING LOOKUP THREAD: No cached credentials found for %s:%s",
                   data->gateway, data->protocol);
        g_free (secret);
        g_task_return_pointer (task, NULL, NULL);
        return;
    }

    g_message ("KEYRING LOOKUP THREAD: FOUND credentials for %s:%s (secret length=%zu)",
               data->gateway, data->protocol, strlen (secret));

    /* Parse JSON */
    VpnSsoCachedCredential *cred = deserialize_credential (secret);

    /* Securely clear and free the secret string */
    memset (secret, 0, strlen (secret));
    g_free (secret);

    if (!cred) {
        g_warning ("KEYRING LOOKUP THREAD: Failed to parse cached credentials");
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Failed to parse cached credentials");
        return;
    }

    /* Check expiration */
    gint64 now = g_get_real_time () / G_USEC_PER_SEC;
    if (cred->expires_at > 0 && now >= cred->expires_at) {
        g_message ("KEYRING LOOKUP THREAD: Cached credentials expired - will be cleared");
        vpn_sso_cached_credential_free (cred);
        g_task_return_pointer (task, NULL, NULL);
        return;
    }

    gint64 remaining = cred->expires_at - now;
    g_message ("KEYRING LOOKUP THREAD: Valid credentials (expires in %" G_GINT64_FORMAT " seconds)", remaining);

    g_task_return_pointer (task, cred, (GDestroyNotify) vpn_sso_cached_credential_free);
}

/**
 * vpn_sso_credential_cache_lookup_async:
 *
 * Looks up cached credentials from GNOME Keyring.
 */
void
vpn_sso_credential_cache_lookup_async (const gchar         *gateway,
                                        const gchar         *protocol,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
    GTask *task;
    LookupData *data;

    g_return_if_fail (gateway != NULL);
    g_return_if_fail (protocol != NULL);

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_credential_cache_lookup_async);

    g_message ("KEYRING LOOKUP: gateway=%s protocol=%s", gateway, protocol);

    /* Store data for thread */
    data = g_new0 (LookupData, 1);
    data->gateway = g_strdup (gateway);
    data->protocol = g_strdup (protocol);
    g_task_set_task_data (task, data, (GDestroyNotify) lookup_data_free);

    /* Run in thread */
    g_task_run_in_thread (task, lookup_thread_func);
    g_object_unref (task);
}

/**
 * vpn_sso_credential_cache_lookup_finish:
 *
 * Completes credential lookup.
 */
VpnSsoCachedCredential *
vpn_sso_credential_cache_lookup_finish (GAsyncResult  *result,
                                         GError       **error)
{
    g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
}

/*
 * Data structure for clear operation
 */
typedef struct {
    gchar *gateway;
    gchar *protocol;
} ClearData;

static void
clear_data_free (ClearData *data)
{
    if (data) {
        g_free (data->gateway);
        g_free (data->protocol);
        g_free (data);
    }
}

/*
 * Thread function for clearing credentials using secret-tool.
 */
static void
clear_thread_func (GTask        *task,
                   gpointer      source_object G_GNUC_UNUSED,
                   gpointer      task_data,
                   GCancellable *cancellable G_GNUC_UNUSED)
{
    ClearData *data = task_data;
    GError *error = NULL;

    g_message ("KEYRING CLEAR THREAD: Clearing credentials for %s:%s",
               data->gateway, data->protocol);

    /* Build secret-tool clear command */
    const gchar *argv[] = {
        "/usr/bin/secret-tool", "clear",
        "xdg:schema", "org.freedesktop.NetworkManager.vpn-sso",
        "gateway", data->gateway,
        "protocol", data->protocol,
        NULL
    };

    gchar *result = run_secret_tool (argv, NULL, &error);
    g_free (result);

    if (error) {
        g_warning ("KEYRING CLEAR THREAD: Error clearing credentials: %s", error->message);
        g_error_free (error);
        /* Don't fail the task - clearing is best effort */
    } else {
        g_message ("KEYRING CLEAR THREAD: SUCCESS - credentials cleared for %s:%s",
                   data->gateway, data->protocol);
    }

    g_task_return_boolean (task, TRUE);
}

/**
 * vpn_sso_credential_cache_clear_async:
 *
 * Clears cached credentials for a gateway from GNOME Keyring.
 */
void
vpn_sso_credential_cache_clear_async (const gchar         *gateway,
                                       const gchar         *protocol,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
    GTask *task;
    ClearData *data;

    g_return_if_fail (gateway != NULL);
    g_return_if_fail (protocol != NULL);

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_credential_cache_clear_async);

    g_message ("KEYRING CLEAR: Clearing credentials for %s:%s", gateway, protocol);

    /* Store data for thread */
    data = g_new0 (ClearData, 1);
    data->gateway = g_strdup (gateway);
    data->protocol = g_strdup (protocol);
    g_task_set_task_data (task, data, (GDestroyNotify) clear_data_free);

    /* Run in thread */
    g_task_run_in_thread (task, clear_thread_func);
    g_object_unref (task);
}

/**
 * vpn_sso_credential_cache_clear_finish:
 *
 * Completes credential clearing.
 */
gboolean
vpn_sso_credential_cache_clear_finish (GAsyncResult  *result,
                                        GError       **error)
{
    g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
}

/*
 * Thread function for clearing all credentials using secret-tool.
 */
static void
clear_all_thread_func (GTask        *task,
                       gpointer      source_object G_GNUC_UNUSED,
                       gpointer      task_data G_GNUC_UNUSED,
                       GCancellable *cancellable G_GNUC_UNUSED)
{
    GError *error = NULL;

    g_message ("KEYRING CLEAR ALL THREAD: Clearing all VPN SSO credentials");

    /* Build secret-tool clear command - clear all items with our schema */
    const gchar *argv[] = {
        "/usr/bin/secret-tool", "clear",
        "xdg:schema", "org.freedesktop.NetworkManager.vpn-sso",
        NULL
    };

    gchar *result = run_secret_tool (argv, NULL, &error);
    g_free (result);

    if (error) {
        g_warning ("KEYRING CLEAR ALL THREAD: Error clearing credentials: %s", error->message);
        g_error_free (error);
    } else {
        g_message ("KEYRING CLEAR ALL THREAD: SUCCESS - all credentials cleared");
    }

    g_task_return_boolean (task, TRUE);
}

/**
 * vpn_sso_credential_cache_clear_all_async:
 *
 * Clears all cached VPN SSO credentials from GNOME Keyring.
 */
void
vpn_sso_credential_cache_clear_all_async (GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
    GTask *task;

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_credential_cache_clear_all_async);

    g_message ("KEYRING CLEAR ALL: Clearing all VPN SSO credentials");

    /* Run in thread */
    g_task_run_in_thread (task, clear_all_thread_func);
    g_object_unref (task);
}

/**
 * vpn_sso_credential_cache_clear_all_finish:
 *
 * Completes clearing all credentials.
 */
gboolean
vpn_sso_credential_cache_clear_all_finish (GAsyncResult  *result,
                                            GError       **error)
{
    g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
}
