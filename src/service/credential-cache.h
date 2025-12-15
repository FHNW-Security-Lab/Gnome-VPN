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

#ifndef __CREDENTIAL_CACHE_H__
#define __CREDENTIAL_CACHE_H__

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* Default cache duration: 8 hours */
#define VPN_SSO_DEFAULT_CACHE_DURATION_HOURS 8

/**
 * VpnSsoCachedCredential:
 * @gateway: VPN gateway address
 * @protocol: VPN protocol (globalprotect/anyconnect)
 * @username: User's username
 * @cookie: SSO authentication cookie
 * @fingerprint: Server certificate fingerprint (AnyConnect)
 * @usergroup: User group for GlobalProtect
 * @created_at: Unix timestamp when credential was created
 * @expires_at: Unix timestamp when credential expires
 *
 * Structure holding cached VPN SSO credentials.
 */
typedef struct {
    gchar   *gateway;
    gchar   *protocol;
    gchar   *username;
    gchar   *cookie;
    gchar   *fingerprint;
    gchar   *usergroup;
    gint64   created_at;
    gint64   expires_at;
} VpnSsoCachedCredential;

/**
 * vpn_sso_cached_credential_free:
 * @credential: A #VpnSsoCachedCredential
 *
 * Frees a cached credential structure.
 */
void vpn_sso_cached_credential_free (VpnSsoCachedCredential *credential);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (VpnSsoCachedCredential, vpn_sso_cached_credential_free)

/**
 * vpn_sso_credential_cache_store_async:
 * @gateway: VPN gateway address
 * @protocol: VPN protocol
 * @username: (nullable): Username
 * @cookie: SSO cookie to store
 * @fingerprint: (nullable): Server fingerprint
 * @usergroup: (nullable): User group
 * @cache_hours: Number of hours to cache (0 = use default)
 * @cancellable: (nullable): A #GCancellable
 * @callback: Callback function
 * @user_data: User data for callback
 *
 * Stores SSO credentials in the secure keyring.
 */
void vpn_sso_credential_cache_store_async (const gchar         *gateway,
                                            const gchar         *protocol,
                                            const gchar         *username,
                                            const gchar         *cookie,
                                            const gchar         *fingerprint,
                                            const gchar         *usergroup,
                                            gint                 cache_hours,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);

/**
 * vpn_sso_credential_cache_store_finish:
 * @result: A #GAsyncResult
 * @error: Return location for error
 *
 * Completes the store operation.
 *
 * Returns: %TRUE if successful
 */
gboolean vpn_sso_credential_cache_store_finish (GAsyncResult  *result,
                                                 GError       **error);

/**
 * vpn_sso_credential_cache_lookup_async:
 * @gateway: VPN gateway address
 * @protocol: VPN protocol
 * @cancellable: (nullable): A #GCancellable
 * @callback: Callback function
 * @user_data: User data for callback
 *
 * Looks up cached SSO credentials for the given gateway.
 */
void vpn_sso_credential_cache_lookup_async (const gchar         *gateway,
                                             const gchar         *protocol,
                                             GCancellable        *cancellable,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data);

/**
 * vpn_sso_credential_cache_lookup_finish:
 * @result: A #GAsyncResult
 * @error: Return location for error
 *
 * Completes the lookup operation.
 *
 * Returns: (transfer full) (nullable): The cached credential or %NULL if not found/expired
 */
VpnSsoCachedCredential *vpn_sso_credential_cache_lookup_finish (GAsyncResult  *result,
                                                                 GError       **error);

/**
 * vpn_sso_credential_cache_clear_async:
 * @gateway: VPN gateway address
 * @protocol: VPN protocol
 * @cancellable: (nullable): A #GCancellable
 * @callback: Callback function
 * @user_data: User data for callback
 *
 * Removes cached credentials for the given gateway.
 */
void vpn_sso_credential_cache_clear_async (const gchar         *gateway,
                                            const gchar         *protocol,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);

/**
 * vpn_sso_credential_cache_clear_finish:
 * @result: A #GAsyncResult
 * @error: Return location for error
 *
 * Completes the clear operation.
 *
 * Returns: %TRUE if successful
 */
gboolean vpn_sso_credential_cache_clear_finish (GAsyncResult  *result,
                                                 GError       **error);

/**
 * vpn_sso_credential_cache_clear_all_async:
 * @cancellable: (nullable): A #GCancellable
 * @callback: Callback function
 * @user_data: User data for callback
 *
 * Removes all cached VPN SSO credentials.
 */
void vpn_sso_credential_cache_clear_all_async (GCancellable        *cancellable,
                                                GAsyncReadyCallback  callback,
                                                gpointer             user_data);

/**
 * vpn_sso_credential_cache_clear_all_finish:
 * @result: A #GAsyncResult
 * @error: Return location for error
 *
 * Completes the clear all operation.
 *
 * Returns: %TRUE if successful
 */
gboolean vpn_sso_credential_cache_clear_all_finish (GAsyncResult  *result,
                                                     GError       **error);

G_END_DECLS

#endif /* __CREDENTIAL_CACHE_H__ */
