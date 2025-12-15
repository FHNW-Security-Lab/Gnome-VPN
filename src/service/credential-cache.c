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
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <glib/gstdio.h>

/**
 * SECTION:credential-cache
 * @title: Credential Cache
 * @short_description: File-based storage for VPN SSO credentials
 *
 * This module provides secure file-based storage for VPN SSO credentials.
 * Credentials are stored in JSON files in the user's home directory with
 * restricted permissions (0600). Each gateway/protocol combination gets
 * its own cache file.
 *
 * Files are stored in: ~/.cache/gnome-vpn-sso/
 */

#define CACHE_DIR_NAME ".cache/gnome-vpn-sso"

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
 * Get the user's home directory from session environment
 */
static gchar *
get_user_home_dir (void)
{
    g_autoptr(VpnSsoSessionEnv) session_env = vpn_sso_get_graphical_session_env ();

    if (session_env && session_env->home)
        return g_strdup (session_env->home);

    /* Fallback to env or /tmp */
    const gchar *home = g_getenv ("HOME");
    if (home)
        return g_strdup (home);

    return g_strdup ("/tmp");
}

/*
 * Get the cache directory path, creating it if needed
 * When running as root, we temporarily drop privileges to the target user
 * to create the directory with proper ownership.
 */
static gchar *
get_cache_dir (void)
{
    g_autoptr(VpnSsoSessionEnv) session_env = vpn_sso_get_graphical_session_env ();
    g_autofree gchar *home = NULL;
    gchar *cache_dir = NULL;
    uid_t target_uid = 0;
    gid_t target_gid = 0;
    uid_t original_euid = geteuid ();
    gid_t original_egid = getegid ();
    gboolean dropped_privs = FALSE;

    g_message ("get_cache_dir: running as euid=%d egid=%d", original_euid, original_egid);

    if (session_env && session_env->home) {
        home = g_strdup (session_env->home);
        target_uid = session_env->uid;
        /* Get the user's primary group */
        struct passwd *pw = getpwuid (target_uid);
        if (pw)
            target_gid = pw->pw_gid;
        g_message ("get_cache_dir: session_env found - home=%s uid=%d gid=%d",
                   home, target_uid, target_gid);
    } else {
        const gchar *env_home = g_getenv ("HOME");
        home = env_home ? g_strdup (env_home) : g_strdup ("/tmp");
        g_message ("get_cache_dir: NO session_env, fallback home=%s", home);
    }

    cache_dir = g_build_filename (home, CACHE_DIR_NAME, NULL);
    g_message ("get_cache_dir: cache_dir=%s", cache_dir);

    /* If running as root and we have a target user, drop privileges temporarily */
    if (original_euid == 0 && target_uid >= 1000 && target_gid > 0) {
        g_message ("get_cache_dir: attempting to drop privileges to uid=%d gid=%d", target_uid, target_gid);
        if (setegid (target_gid) == 0 && seteuid (target_uid) == 0) {
            dropped_privs = TRUE;
            g_message ("get_cache_dir: privileges dropped successfully");
        } else {
            g_warning ("Failed to drop privileges for cache directory creation: %s", g_strerror (errno));
        }
    } else {
        g_message ("get_cache_dir: not dropping privileges (euid=%d, target_uid=%d, target_gid=%d)",
                   original_euid, target_uid, target_gid);
    }

    /* Create directory if it doesn't exist */
    if (g_mkdir_with_parents (cache_dir, 0700) != 0 && errno != EEXIST) {
        int save_errno = errno;
        g_warning ("Failed to create cache directory %s: %s", cache_dir, g_strerror (save_errno));

        /* Restore privileges before trying fallback */
        if (dropped_privs) {
            seteuid (original_euid);
            setegid (original_egid);
            dropped_privs = FALSE;
            g_message ("get_cache_dir: privileges restored for fallback");
        }

        /* Fallback to XDG_RUNTIME_DIR which should always be writable */
        g_free (cache_dir);
        if (session_env && session_env->xdg_runtime_dir) {
            cache_dir = g_build_filename (session_env->xdg_runtime_dir, "gnome-vpn-sso", NULL);
            g_message ("get_cache_dir: trying fallback to XDG_RUNTIME_DIR: %s", cache_dir);

            /* Try to create the fallback directory */
            if (g_mkdir_with_parents (cache_dir, 0700) != 0 && errno != EEXIST) {
                g_warning ("Failed to create fallback cache directory %s: %s", cache_dir, g_strerror (errno));
                g_free (cache_dir);
                cache_dir = NULL;
            } else {
                g_message ("get_cache_dir: fallback directory created/exists: %s", cache_dir);
            }
        } else {
            cache_dir = NULL;
        }
    } else {
        g_message ("get_cache_dir: directory created/exists: %s", cache_dir);
    }

    /* Restore privileges if we dropped them */
    if (dropped_privs) {
        if (seteuid (original_euid) != 0 || setegid (original_egid) != 0) {
            g_warning ("Failed to restore privileges: %s", g_strerror (errno));
        } else {
            g_message ("get_cache_dir: privileges restored");
        }
    }

    return cache_dir;
}

/*
 * Generate a filename for the cache entry
 */
static gchar *
get_cache_filename (const gchar *gateway, const gchar *protocol)
{
    g_autofree gchar *cache_dir = get_cache_dir ();
    if (!cache_dir)
        return NULL;

    /* Use a hash of gateway+protocol for filename */
    g_autofree gchar *key = g_strdup_printf ("%s:%s", gateway, protocol);
    g_autofree gchar *hash = g_compute_checksum_for_string (G_CHECKSUM_SHA256, key, -1);

    return g_build_filename (cache_dir, hash, NULL);
}

/*
 * Serialize credential data to JSON for storage
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

    gint start, end;
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
 * Helper structure for privilege management
 */
typedef struct {
    uid_t original_euid;
    gid_t original_egid;
    gboolean dropped;
} PrivilegeState;

/*
 * Drop privileges to the target user for file operations
 */
static gboolean
drop_privileges_for_user (PrivilegeState *state)
{
    g_autoptr(VpnSsoSessionEnv) session_env = NULL;

    state->original_euid = geteuid ();
    state->original_egid = getegid ();
    state->dropped = FALSE;

    if (state->original_euid != 0)
        return TRUE; /* Not root, nothing to do */

    session_env = vpn_sso_get_graphical_session_env ();
    if (!session_env || session_env->uid < 1000)
        return TRUE; /* No valid user found */

    struct passwd *pw = getpwuid (session_env->uid);
    if (!pw)
        return TRUE;

    if (setegid (pw->pw_gid) == 0 && seteuid (session_env->uid) == 0) {
        state->dropped = TRUE;
        g_debug ("Dropped privileges to UID %d for file operation", session_env->uid);
        return TRUE;
    }

    g_warning ("Failed to drop privileges: %s", g_strerror (errno));
    return FALSE;
}

/*
 * Restore privileges after file operations
 */
static void
restore_privileges (PrivilegeState *state)
{
    if (!state->dropped)
        return;

    if (seteuid (state->original_euid) != 0 || setegid (state->original_egid) != 0) {
        g_warning ("Failed to restore privileges: %s", g_strerror (errno));
    }
}

/**
 * vpn_sso_credential_cache_store_async:
 *
 * Stores VPN SSO credentials in the cache.
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
    g_autoptr(GError) error = NULL;
    PrivilegeState priv_state = { 0 };

    g_return_if_fail (gateway != NULL);
    g_return_if_fail (protocol != NULL);

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_credential_cache_store_async);

    if (cache_hours <= 0)
        cache_hours = VPN_SSO_DEFAULT_CACHE_DURATION_HOURS;

    /* Calculate timestamps */
    gint64 now = g_get_real_time () / G_USEC_PER_SEC;
    gint64 expires_at = now + (cache_hours * 3600);

    g_message ("CACHE STORE: gateway=%s protocol=%s username=%s cookie=%s (expires in %d hours)",
               gateway, protocol, username ? username : "(null)",
               cookie ? "(present)" : "(null)",
               cache_hours > 0 ? cache_hours : VPN_SSO_DEFAULT_CACHE_DURATION_HOURS);

    /* Serialize to JSON */
    g_autofree gchar *json = serialize_credential (gateway, protocol, username,
                                                   cookie, fingerprint, usergroup,
                                                   now, expires_at);

    g_message ("CACHE STORE: JSON length=%zu", json ? strlen(json) : 0);

    /* Get cache filename (this also creates dir with proper privileges) */
    g_autofree gchar *filename = get_cache_filename (gateway, protocol);
    if (!filename) {
        g_warning ("CACHE STORE: Failed to get cache filename!");
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "Failed to get cache filename");
        g_object_unref (task);
        return;
    }

    g_message ("CACHE STORE: filename=%s", filename);

    /* Drop privileges for file write operation */
    drop_privileges_for_user (&priv_state);

    /* Write to file with restricted permissions */
    if (!g_file_set_contents (filename, json, -1, &error)) {
        g_warning ("CACHE STORE: Failed to write file: %s", error->message);
        restore_privileges (&priv_state);
        g_task_return_error (task, g_steal_pointer (&error));
        g_object_unref (task);
        return;
    }

    /* Set file permissions to 0600 */
    if (chmod (filename, 0600) != 0) {
        g_warning ("Failed to set cache file permissions: %s", g_strerror (errno));
    }

    restore_privileges (&priv_state);

    g_message ("CACHE STORE: SUCCESS - credentials stored in %s", filename);
    g_task_return_boolean (task, TRUE);
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

/**
 * vpn_sso_credential_cache_lookup_async:
 *
 * Looks up cached credentials for a gateway.
 */
void
vpn_sso_credential_cache_lookup_async (const gchar         *gateway,
                                        const gchar         *protocol,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
    GTask *task;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *contents = NULL;
    VpnSsoCachedCredential *cred = NULL;

    g_return_if_fail (gateway != NULL);
    g_return_if_fail (protocol != NULL);

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_credential_cache_lookup_async);

    g_message ("CACHE LOOKUP: gateway=%s protocol=%s", gateway, protocol);

    /* Get cache filename */
    g_autofree gchar *filename = get_cache_filename (gateway, protocol);
    if (!filename) {
        g_warning ("CACHE LOOKUP: Failed to get cache filename!");
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                 "Failed to get cache filename");
        g_object_unref (task);
        return;
    }

    g_message ("CACHE LOOKUP: filename=%s", filename);

    /* Read file */
    if (!g_file_get_contents (filename, &contents, NULL, &error)) {
        if (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_message ("CACHE LOOKUP: No cached credentials found (file does not exist)");
            g_task_return_pointer (task, NULL, NULL);
        } else {
            g_warning ("CACHE LOOKUP: Error reading file: %s", error->message);
            g_task_return_error (task, g_steal_pointer (&error));
        }
        g_object_unref (task);
        return;
    }

    g_message ("CACHE LOOKUP: File read successfully, length=%zu", contents ? strlen(contents) : 0);

    /* Parse JSON */
    cred = deserialize_credential (contents);
    if (!cred) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Failed to parse cached credentials");
        g_object_unref (task);
        return;
    }

    /* Check expiration */
    gint64 now = g_get_real_time () / G_USEC_PER_SEC;
    if (cred->expires_at > 0 && now >= cred->expires_at) {
        g_message ("Cached credentials expired");
        vpn_sso_cached_credential_free (cred);
        /* Delete expired file */
        g_unlink (filename);
        g_task_return_pointer (task, NULL, NULL);
        g_object_unref (task);
        return;
    }

    gint64 remaining = cred->expires_at - now;
    g_message ("Found valid cached credentials (expires in %" G_GINT64_FORMAT " seconds)", remaining);

    g_task_return_pointer (task, cred, (GDestroyNotify) vpn_sso_cached_credential_free);
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

/**
 * vpn_sso_credential_cache_clear_async:
 *
 * Clears cached credentials for a gateway.
 */
void
vpn_sso_credential_cache_clear_async (const gchar         *gateway,
                                       const gchar         *protocol,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
    GTask *task;

    g_return_if_fail (gateway != NULL);
    g_return_if_fail (protocol != NULL);

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_credential_cache_clear_async);

    g_debug ("Clearing cached credential for %s:%s", gateway, protocol);

    g_autofree gchar *filename = get_cache_filename (gateway, protocol);
    if (filename) {
        g_unlink (filename);
    }

    g_task_return_boolean (task, TRUE);
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

/**
 * vpn_sso_credential_cache_clear_all_async:
 *
 * Clears all cached credentials.
 */
void
vpn_sso_credential_cache_clear_all_async (GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
    GTask *task;
    g_autoptr(GDir) dir = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *name;

    task = g_task_new (NULL, cancellable, callback, user_data);
    g_task_set_source_tag (task, vpn_sso_credential_cache_clear_all_async);

    g_debug ("Clearing all cached credentials");

    g_autofree gchar *cache_dir = get_cache_dir ();
    if (!cache_dir) {
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    dir = g_dir_open (cache_dir, 0, &error);
    if (!dir) {
        /* No directory = no cached credentials */
        g_task_return_boolean (task, TRUE);
        g_object_unref (task);
        return;
    }

    while ((name = g_dir_read_name (dir)) != NULL) {
        g_autofree gchar *path = g_build_filename (cache_dir, name, NULL);
        g_unlink (path);
    }

    g_task_return_boolean (task, TRUE);
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
