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

#ifndef __NM_VPN_SSO_AUTH_DIALOG_H__
#define __NM_VPN_SSO_AUTH_DIALOG_H__

#include <gtk/gtk.h>
#include <adwaita.h>

G_BEGIN_DECLS

#define NM_TYPE_VPN_SSO_AUTH_DIALOG            (nm_vpn_sso_auth_dialog_get_type ())
G_DECLARE_FINAL_TYPE (NmVpnSsoAuthDialog, nm_vpn_sso_auth_dialog, NM, VPN_SSO_AUTH_DIALOG, AdwWindow)

/**
 * NmVpnSsoAuthDialog:
 *
 * An authentication dialog for SSO-based VPN authentication.
 * Uses WebKitGTK to display the SSO login page and capture authentication tokens.
 */

/**
 * nm_vpn_sso_auth_dialog_new:
 * @gateway: The VPN gateway URL
 * @protocol: The VPN protocol (globalprotect or anyconnect)
 *
 * Creates a new authentication dialog.
 *
 * Returns: (transfer full): A new #NmVpnSsoAuthDialog
 */
NmVpnSsoAuthDialog *nm_vpn_sso_auth_dialog_new (const char *gateway,
                                                 const char *protocol);

/**
 * nm_vpn_sso_auth_dialog_get_cookie:
 * @dialog: A #NmVpnSsoAuthDialog
 *
 * Gets the authentication cookie captured from the SSO flow.
 *
 * Returns: (transfer none) (nullable): The authentication cookie, or NULL if not yet captured
 */
const char *nm_vpn_sso_auth_dialog_get_cookie (NmVpnSsoAuthDialog *dialog);

/**
 * nm_vpn_sso_auth_dialog_get_username:
 * @dialog: A #NmVpnSsoAuthDialog
 *
 * Gets the username from the SSO authentication, if available.
 *
 * Returns: (transfer none) (nullable): The username, or NULL if not available
 */
const char *nm_vpn_sso_auth_dialog_get_username (NmVpnSsoAuthDialog *dialog);

G_END_DECLS

#endif /* __NM_VPN_SSO_AUTH_DIALOG_H__ */
