/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * libnm VPN plugin for vpn-sso
 * Required by NetworkManager/libnm to recognize the VPN type
 */

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <NetworkManager.h>

#define VPN_SSO_SERVICE_TYPE "org.freedesktop.NetworkManager.vpn-sso"

#define NM_TYPE_VPN_SSO_PLUGIN            (nm_vpn_sso_plugin_get_type())
#define NM_VPN_SSO_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), NM_TYPE_VPN_SSO_PLUGIN, NmVpnSsoPlugin))

typedef struct {
    GObject parent;
} NmVpnSsoPlugin;

typedef struct {
    GObjectClass parent;
} NmVpnSsoPluginClass;

enum {
    PROP_0,
    PROP_NAME,
    PROP_DESC,
    PROP_SERVICE,
    LAST_PROP
};

static void nm_vpn_sso_plugin_interface_init(NMVpnEditorPluginInterface *iface);

G_DEFINE_TYPE_WITH_CODE(NmVpnSsoPlugin, nm_vpn_sso_plugin, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(NM_TYPE_VPN_EDITOR_PLUGIN,
                                              nm_vpn_sso_plugin_interface_init))

static NMVpnEditorPluginCapability
get_capabilities(NMVpnEditorPlugin *plugin)
{
    (void)plugin;
    return NM_VPN_EDITOR_PLUGIN_CAPABILITY_IPV6;
}

static NMVpnEditor *
get_editor(NMVpnEditorPlugin *plugin, NMConnection *connection, GError **error)
{
    (void)plugin;
    (void)connection;
    g_set_error(error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_FAILED,
                "Editor not available in this plugin");
    return NULL;
}

static void
nm_vpn_sso_plugin_interface_init(NMVpnEditorPluginInterface *iface)
{
    iface->get_capabilities = get_capabilities;
    iface->get_editor = get_editor;
}

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    (void)object;

    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string(value, "SSO VPN (GlobalProtect/AnyConnect)");
        break;
    case PROP_DESC:
        g_value_set_string(value, "Compatible with GlobalProtect and Cisco AnyConnect SSO VPNs");
        break;
    case PROP_SERVICE:
        g_value_set_string(value, VPN_SSO_SERVICE_TYPE);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
nm_vpn_sso_plugin_init(NmVpnSsoPlugin *self)
{
    (void)self;
}

static void
nm_vpn_sso_plugin_class_init(NmVpnSsoPluginClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = get_property;

    g_object_class_override_property(object_class, PROP_NAME, NM_VPN_EDITOR_PLUGIN_NAME);
    g_object_class_override_property(object_class, PROP_DESC, NM_VPN_EDITOR_PLUGIN_DESCRIPTION);
    g_object_class_override_property(object_class, PROP_SERVICE, NM_VPN_EDITOR_PLUGIN_SERVICE);
}

G_MODULE_EXPORT NMVpnEditorPlugin *
nm_vpn_editor_plugin_factory(GError **error)
{
    (void)error;
    return NM_VPN_EDITOR_PLUGIN(g_object_new(NM_TYPE_VPN_SSO_PLUGIN, NULL));
}
