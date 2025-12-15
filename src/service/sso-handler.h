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

#ifndef __SSO_HANDLER_H__
#define __SSO_HANDLER_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define VPN_SSO_TYPE_HANDLER            (vpn_sso_handler_get_type ())
#define VPN_SSO_HANDLER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), VPN_SSO_TYPE_HANDLER, VpnSsoHandler))
#define VPN_SSO_HANDLER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), VPN_SSO_TYPE_HANDLER, VpnSsoHandlerClass))
#define VPN_SSO_IS_HANDLER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), VPN_SSO_TYPE_HANDLER))
#define VPN_SSO_IS_HANDLER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), VPN_SSO_TYPE_HANDLER))
#define VPN_SSO_HANDLER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), VPN_SSO_TYPE_HANDLER, VpnSsoHandlerClass))

typedef struct _VpnSsoHandler        VpnSsoHandler;
typedef struct _VpnSsoHandlerClass   VpnSsoHandlerClass;
typedef struct _VpnSsoHandlerPrivate VpnSsoHandlerPrivate;

/**
 * VpnSsoProtocol:
 * @VPN_SSO_PROTOCOL_GLOBALPROTECT: GlobalProtect (Palo Alto) protocol
 * @VPN_SSO_PROTOCOL_ANYCONNECT: AnyConnect (Cisco) protocol
 *
 * VPN protocols supported by the SSO handler.
 */
typedef enum {
    VPN_SSO_PROTOCOL_GLOBALPROTECT,
    VPN_SSO_PROTOCOL_ANYCONNECT
} VpnSsoProtocol;

/**
 * VpnSsoCredentials:
 * @protocol: The VPN protocol used
 * @gateway: The VPN gateway address
 * @username: Username (may be NULL)
 * @cookie: Authentication cookie for GlobalProtect
 * @usergroup: Usergroup parameter for GlobalProtect
 * @success: Whether authentication was successful
 * @error_message: Error message if authentication failed
 *
 * Credentials obtained from SSO authentication.
 */
typedef struct {
    VpnSsoProtocol  protocol;
    gchar          *gateway;
    gchar          *username;
    gchar          *cookie;
    gchar          *usergroup;
    gboolean        success;
    gchar          *error_message;
} VpnSsoCredentials;

/**
 * VpnSsoHandler:
 *
 * Main SSO authentication handler object.
 */
struct _VpnSsoHandler {
    GObject parent;
    VpnSsoHandlerPrivate *priv;
};

struct _VpnSsoHandlerClass {
    GObjectClass parent_class;
};

GType vpn_sso_handler_get_type (void);

/**
 * vpn_sso_handler_new:
 *
 * Creates a new SSO handler instance.
 *
 * Returns: A new #VpnSsoHandler (transfer full)
 */
VpnSsoHandler *vpn_sso_handler_new (void);

/**
 * vpn_sso_handler_authenticate_async:
 * @handler: The #VpnSsoHandler
 * @protocol: The VPN protocol to use
 * @gateway: The VPN gateway address
 * @username: Optional username (may be NULL)
 * @cancellable: Optional #GCancellable
 * @callback: Callback to invoke when operation completes
 * @user_data: User data for the callback
 *
 * Initiates SSO authentication asynchronously. This will spawn the
 * appropriate backend (gp-saml-gui or openconnect-sso) and capture
 * the authentication credentials.
 */
void vpn_sso_handler_authenticate_async (VpnSsoHandler       *handler,
                                         VpnSsoProtocol       protocol,
                                         const gchar         *gateway,
                                         const gchar         *username,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data);

/**
 * vpn_sso_handler_authenticate_finish:
 * @handler: The #VpnSsoHandler
 * @result: The #GAsyncResult
 * @error: Location for error information
 *
 * Completes an asynchronous authentication operation.
 *
 * Returns: A #VpnSsoCredentials structure (transfer full), or NULL on error
 */
VpnSsoCredentials *vpn_sso_handler_authenticate_finish (VpnSsoHandler  *handler,
                                                        GAsyncResult   *result,
                                                        GError        **error);

/**
 * vpn_sso_credentials_new:
 *
 * Creates a new empty credentials structure.
 *
 * Returns: A new #VpnSsoCredentials (transfer full)
 */
VpnSsoCredentials *vpn_sso_credentials_new (void);

/**
 * vpn_sso_credentials_free:
 * @credentials: The #VpnSsoCredentials to free
 *
 * Frees a credentials structure and all its contents.
 */
void vpn_sso_credentials_free (VpnSsoCredentials *credentials);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (VpnSsoCredentials, vpn_sso_credentials_free)

/**
 * vpn_sso_protocol_to_string:
 * @protocol: The #VpnSsoProtocol
 *
 * Converts a protocol enum to a string.
 *
 * Returns: A string representation (do not free)
 */
const gchar *vpn_sso_protocol_to_string (VpnSsoProtocol protocol);

/**
 * vpn_sso_protocol_from_string:
 * @protocol_str: The protocol string
 *
 * Converts a string to a protocol enum.
 *
 * Returns: The #VpnSsoProtocol, or -1 if invalid
 */
VpnSsoProtocol vpn_sso_protocol_from_string (const gchar *protocol_str);

G_END_DECLS

#endif /* __SSO_HANDLER_H__ */
