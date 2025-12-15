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

#ifndef __UTILS_H__
#define __UTILS_H__

#include <glib.h>
#include <sys/types.h>

G_BEGIN_DECLS

/**
 * vpn_sso_utils_get_version:
 *
 * Get the version string of the VPN SSO plugin.
 *
 * Returns: A string containing the version (do not free)
 */
const char *vpn_sso_utils_get_version (void);

/**
 * vpn_sso_utils_init:
 *
 * Initialize the VPN SSO utilities.
 */
void vpn_sso_utils_init (void);

/**
 * vpn_sso_utils_cleanup:
 *
 * Clean up the VPN SSO utilities.
 */
void vpn_sso_utils_cleanup (void);

/**
 * VpnSsoSessionEnv:
 *
 * Structure holding graphical session environment variables.
 */
typedef struct {
    gchar *display;
    gchar *wayland_display;
    gchar *xdg_runtime_dir;
    gchar *xauthority;
    gchar *dbus_session_bus_address;
    gchar *home;
    uid_t  uid;
    gchar *username;
} VpnSsoSessionEnv;

/**
 * vpn_sso_session_env_free:
 * @env: A #VpnSsoSessionEnv structure
 *
 * Frees a session environment structure.
 */
void vpn_sso_session_env_free (VpnSsoSessionEnv *env);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (VpnSsoSessionEnv, vpn_sso_session_env_free)

/**
 * vpn_sso_get_graphical_session_env:
 *
 * Detects the active graphical session and returns its environment.
 * This is useful when the service runs as root but needs to spawn
 * GUI applications in the user's session.
 *
 * Returns: (transfer full): A new #VpnSsoSessionEnv or NULL on failure
 */
VpnSsoSessionEnv *vpn_sso_get_graphical_session_env (void);

G_END_DECLS

#endif /* __UTILS_H__ */
