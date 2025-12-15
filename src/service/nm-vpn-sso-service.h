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

#ifndef __NM_VPN_SSO_SERVICE_H__
#define __NM_VPN_SSO_SERVICE_H__

#include <glib-object.h>
#include <NetworkManager.h>

G_BEGIN_DECLS

#define NM_TYPE_VPN_SSO_SERVICE            (nm_vpn_sso_service_get_type ())
#define NM_VPN_SSO_SERVICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_VPN_SSO_SERVICE, NmVpnSsoService))
#define NM_VPN_SSO_SERVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_VPN_SSO_SERVICE, NmVpnSsoServiceClass))
#define NM_IS_VPN_SSO_SERVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_VPN_SSO_SERVICE))
#define NM_IS_VPN_SSO_SERVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_VPN_SSO_SERVICE))
#define NM_VPN_SSO_SERVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_VPN_SSO_SERVICE, NmVpnSsoServiceClass))

typedef struct _NmVpnSsoService        NmVpnSsoService;
typedef struct _NmVpnSsoServiceClass   NmVpnSsoServiceClass;
typedef struct _NmVpnSsoServicePrivate NmVpnSsoServicePrivate;

struct _NmVpnSsoService {
    NMVpnServicePlugin parent;
    NmVpnSsoServicePrivate *priv;
};

struct _NmVpnSsoServiceClass {
    NMVpnServicePluginClass parent;
};

GType nm_vpn_sso_service_get_type (void);

NmVpnSsoService *nm_vpn_sso_service_new (const char *bus_name);

G_END_DECLS

#endif /* __NM_VPN_SSO_SERVICE_H__ */
