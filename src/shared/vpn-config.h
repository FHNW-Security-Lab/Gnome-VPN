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

#ifndef __VPN_CONFIG_H__
#define __VPN_CONFIG_H__

G_BEGIN_DECLS

/* VPN service name */
#define NM_DBUS_SERVICE_VPN_SSO "org.freedesktop.NetworkManager.vpn-sso"

/* VPN data keys (stored in connection's vpn setting data) */
#define NM_VPN_SSO_KEY_GATEWAY      "gateway"
#define NM_VPN_SSO_KEY_PROTOCOL     "protocol"
#define NM_VPN_SSO_KEY_USERNAME     "username"
#define NM_VPN_SSO_KEY_USERGROUP    "usergroup"
#define NM_VPN_SSO_KEY_EXTRA_ARGS   "extra-args"
#define NM_VPN_SSO_KEY_CACHE_HOURS  "cache-hours"

/* Protocol values */
#define NM_VPN_SSO_PROTOCOL_GLOBALPROTECT "globalprotect"
#define NM_VPN_SSO_PROTOCOL_ANYCONNECT    "anyconnect"

/* Default values */
#define NM_VPN_SSO_DEFAULT_PROTOCOL NM_VPN_SSO_PROTOCOL_GLOBALPROTECT

G_END_DECLS

#endif /* __VPN_CONFIG_H__ */
