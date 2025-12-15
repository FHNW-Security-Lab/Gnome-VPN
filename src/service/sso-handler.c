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
#include "sso-handler.h"
#include "gp-backend.h"
#include "ac-backend.h"

#include <glib.h>
#include <gio/gio.h>

/**
 * SECTION:sso-handler
 * @title: VpnSsoHandler
 * @short_description: Unified SSO authentication handler
 *
 * The #VpnSsoHandler provides a unified interface for SSO authentication
 * across different VPN protocols (GlobalProtect and AnyConnect).
 */

struct _VpnSsoHandlerPrivate {
    /* Currently no private data needed */
    gint placeholder;
};

G_DEFINE_TYPE_WITH_PRIVATE (VpnSsoHandler, vpn_sso_handler, G_TYPE_OBJECT)

/* Forward declarations */
static void vpn_sso_handler_finalize (GObject *object);

static void
vpn_sso_handler_class_init (VpnSsoHandlerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = vpn_sso_handler_finalize;
}

static void
vpn_sso_handler_init (VpnSsoHandler *handler)
{
    handler->priv = vpn_sso_handler_get_instance_private (handler);
}

static void
vpn_sso_handler_finalize (GObject *object)
{
    G_OBJECT_CLASS (vpn_sso_handler_parent_class)->finalize (object);
}

/**
 * vpn_sso_handler_new:
 *
 * Creates a new SSO handler instance.
 *
 * Returns: A new #VpnSsoHandler
 */
VpnSsoHandler *
vpn_sso_handler_new (void)
{
    return g_object_new (VPN_SSO_TYPE_HANDLER, NULL);
}

/**
 * vpn_sso_handler_authenticate_async:
 *
 * Initiates SSO authentication asynchronously.
 */
void
vpn_sso_handler_authenticate_async (VpnSsoHandler       *handler,
                                    VpnSsoProtocol       protocol,
                                    const gchar         *gateway,
                                    const gchar         *username,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
    g_return_if_fail (VPN_SSO_IS_HANDLER (handler));
    g_return_if_fail (gateway != NULL);

    g_debug ("Starting SSO authentication for %s (protocol: %s)",
             gateway,
             vpn_sso_protocol_to_string (protocol));

    /* Delegate to the appropriate backend */
    switch (protocol) {
        case VPN_SSO_PROTOCOL_GLOBALPROTECT:
            vpn_sso_gp_authenticate_async (gateway,
                                          username,
                                          cancellable,
                                          callback,
                                          user_data);
            break;

        case VPN_SSO_PROTOCOL_ANYCONNECT:
            vpn_sso_ac_authenticate_async (gateway,
                                          username,
                                          cancellable,
                                          callback,
                                          user_data);
            break;

        default:
            {
                GTask *task = g_task_new (handler, cancellable, callback, user_data);
                g_task_return_new_error (task,
                                        G_IO_ERROR,
                                        G_IO_ERROR_NOT_SUPPORTED,
                                        "Unsupported VPN protocol: %d",
                                        protocol);
                g_object_unref (task);
            }
            break;
    }
}

/**
 * vpn_sso_handler_authenticate_finish:
 *
 * Completes an asynchronous authentication operation.
 */
VpnSsoCredentials *
vpn_sso_handler_authenticate_finish (VpnSsoHandler  *handler,
                                     GAsyncResult   *result,
                                     GError        **error)
{
    g_return_val_if_fail (VPN_SSO_IS_HANDLER (handler) ||
                          g_task_is_valid (result, NULL),
                          NULL);
    g_return_val_if_fail (G_IS_TASK (result), NULL);

    /* The result comes from the backend, so we just extract it */
    return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * vpn_sso_credentials_new:
 *
 * Creates a new empty credentials structure.
 */
VpnSsoCredentials *
vpn_sso_credentials_new (void)
{
    VpnSsoCredentials *credentials;

    credentials = g_new0 (VpnSsoCredentials, 1);
    credentials->success = FALSE;

    return credentials;
}

/**
 * vpn_sso_credentials_free:
 *
 * Frees a credentials structure and all its contents.
 */
void
vpn_sso_credentials_free (VpnSsoCredentials *credentials)
{
    if (!credentials)
        return;

    g_free (credentials->gateway);
    g_free (credentials->username);
    g_free (credentials->cookie);
    g_free (credentials->usergroup);
    g_free (credentials->error_message);
    g_free (credentials);
}

/**
 * vpn_sso_protocol_to_string:
 *
 * Converts a protocol enum to a string.
 */
const gchar *
vpn_sso_protocol_to_string (VpnSsoProtocol protocol)
{
    switch (protocol) {
        case VPN_SSO_PROTOCOL_GLOBALPROTECT:
            return "globalprotect";
        case VPN_SSO_PROTOCOL_ANYCONNECT:
            return "anyconnect";
        default:
            return "unknown";
    }
}

/**
 * vpn_sso_protocol_from_string:
 *
 * Converts a string to a protocol enum.
 */
VpnSsoProtocol
vpn_sso_protocol_from_string (const gchar *protocol_str)
{
    if (!protocol_str)
        return -1;

    if (g_ascii_strcasecmp (protocol_str, "globalprotect") == 0)
        return VPN_SSO_PROTOCOL_GLOBALPROTECT;
    else if (g_ascii_strcasecmp (protocol_str, "anyconnect") == 0)
        return VPN_SSO_PROTOCOL_ANYCONNECT;

    return -1;
}
