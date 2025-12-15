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

#ifndef __GP_BACKEND_H__
#define __GP_BACKEND_H__

#include <glib.h>
#include <gio/gio.h>
#include "sso-handler.h"

G_BEGIN_DECLS

/**
 * vpn_sso_gp_authenticate_async:
 * @gateway: The VPN gateway address
 * @username: Optional username (may be NULL)
 * @cancellable: Optional #GCancellable
 * @callback: Callback to invoke when operation completes
 * @user_data: User data for the callback
 *
 * Initiates GlobalProtect SSO authentication by spawning gp-saml-gui.
 * This will open a browser window for SAML authentication and capture
 * the prelogin cookie.
 *
 * The operation will timeout after 5 minutes if not completed.
 */
void vpn_sso_gp_authenticate_async (const gchar         *gateway,
                                    const gchar         *username,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data);

/**
 * vpn_sso_gp_authenticate_finish:
 * @result: The #GAsyncResult
 * @error: Location for error information
 *
 * Completes a GlobalProtect authentication operation.
 *
 * Returns: A #VpnSsoCredentials structure (transfer full), or NULL on error
 */
VpnSsoCredentials *vpn_sso_gp_authenticate_finish (GAsyncResult  *result,
                                                   GError       **error);

G_END_DECLS

#endif /* __GP_BACKEND_H__ */
