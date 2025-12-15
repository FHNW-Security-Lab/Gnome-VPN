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

#ifndef __NM_VPN_SSO_EDITOR_H__
#define __NM_VPN_SSO_EDITOR_H__

#include <gtk/gtk.h>
#include <NetworkManager.h>

G_BEGIN_DECLS

/* Editor widget */
#define NM_TYPE_VPN_SSO_EDITOR            (nm_vpn_sso_editor_get_type ())
#define NM_VPN_SSO_EDITOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_VPN_SSO_EDITOR, NmVpnSsoEditor))
#define NM_VPN_SSO_EDITOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_VPN_SSO_EDITOR, NmVpnSsoEditorClass))
#define NM_IS_VPN_SSO_EDITOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_VPN_SSO_EDITOR))
#define NM_IS_VPN_SSO_EDITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_VPN_SSO_EDITOR))
#define NM_VPN_SSO_EDITOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_VPN_SSO_EDITOR, NmVpnSsoEditorClass))

typedef struct _NmVpnSsoEditor        NmVpnSsoEditor;
typedef struct _NmVpnSsoEditorClass   NmVpnSsoEditorClass;

struct _NmVpnSsoEditorClass {
    GObjectClass parent_class;
};

GType nm_vpn_sso_editor_get_type (void);

NMVpnEditor *nm_vpn_sso_editor_new (NMConnection *connection);

/* Editor plugin factory */
#define NM_TYPE_VPN_SSO_EDITOR_PLUGIN            (nm_vpn_sso_editor_plugin_get_type ())
#define NM_VPN_SSO_EDITOR_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_VPN_SSO_EDITOR_PLUGIN, NmVpnSsoEditorPlugin))
#define NM_VPN_SSO_EDITOR_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_VPN_SSO_EDITOR_PLUGIN, NmVpnSsoEditorPluginClass))
#define NM_IS_VPN_SSO_EDITOR_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_VPN_SSO_EDITOR_PLUGIN))
#define NM_IS_VPN_SSO_EDITOR_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_VPN_SSO_EDITOR_PLUGIN))
#define NM_VPN_SSO_EDITOR_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_VPN_SSO_EDITOR_PLUGIN, NmVpnSsoEditorPluginClass))

typedef struct _NmVpnSsoEditorPlugin        NmVpnSsoEditorPlugin;
typedef struct _NmVpnSsoEditorPluginClass   NmVpnSsoEditorPluginClass;

GType nm_vpn_sso_editor_plugin_get_type (void);

G_END_DECLS

#endif /* __NM_VPN_SSO_EDITOR_H__ */
