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

#ifndef __OPENCONNECT_RUNNER_H__
#define __OPENCONNECT_RUNNER_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define OC_TYPE_RUNNER            (oc_runner_get_type ())
#define OC_RUNNER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), OC_TYPE_RUNNER, OcRunner))
#define OC_RUNNER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), OC_TYPE_RUNNER, OcRunnerClass))
#define OC_IS_RUNNER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), OC_TYPE_RUNNER))
#define OC_IS_RUNNER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), OC_TYPE_RUNNER))
#define OC_RUNNER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), OC_TYPE_RUNNER, OcRunnerClass))

typedef struct _OcRunner        OcRunner;
typedef struct _OcRunnerClass   OcRunnerClass;
typedef struct _OcRunnerPrivate OcRunnerPrivate;

/**
 * OcRunnerState:
 * @OC_RUNNER_STATE_IDLE: Not connected
 * @OC_RUNNER_STATE_STARTING: Initializing connection
 * @OC_RUNNER_STATE_AUTHENTICATING: Performing authentication
 * @OC_RUNNER_STATE_CONNECTING: Establishing tunnel
 * @OC_RUNNER_STATE_CONNECTED: Tunnel active
 * @OC_RUNNER_STATE_DISCONNECTING: Closing connection
 * @OC_RUNNER_STATE_FAILED: Connection failed
 *
 * Connection state for OpenConnect runner.
 */
typedef enum {
    OC_RUNNER_STATE_IDLE,
    OC_RUNNER_STATE_STARTING,
    OC_RUNNER_STATE_AUTHENTICATING,
    OC_RUNNER_STATE_CONNECTING,
    OC_RUNNER_STATE_CONNECTED,
    OC_RUNNER_STATE_DISCONNECTING,
    OC_RUNNER_STATE_FAILED
} OcRunnerState;

/**
 * OcRunnerProtocol:
 * @OC_RUNNER_PROTOCOL_GLOBALPROTECT: GlobalProtect protocol
 * @OC_RUNNER_PROTOCOL_ANYCONNECT: AnyConnect protocol
 *
 * VPN protocol types supported by OpenConnect.
 */
typedef enum {
    OC_RUNNER_PROTOCOL_GLOBALPROTECT,
    OC_RUNNER_PROTOCOL_ANYCONNECT
} OcRunnerProtocol;

/**
 * OcRunner:
 *
 * Object for managing OpenConnect VPN connections.
 */
struct _OcRunner {
    GObject parent;
    OcRunnerPrivate *priv;
};

struct _OcRunnerClass {
    GObjectClass parent;

    /* Signals */
    void (*state_changed)    (OcRunner      *runner,
                              OcRunnerState  state);
    void (*tunnel_ready)     (OcRunner      *runner,
                              const char    *ip4_address,
                              const char    *ip6_address,
                              GHashTable    *config);
    void (*log_message)      (OcRunner      *runner,
                              const char    *message);
    void (*error_occurred)   (OcRunner      *runner,
                              GError        *error);
};

GType oc_runner_get_type (void);

OcRunner *oc_runner_new (void);

gboolean oc_runner_connect (OcRunner          *runner,
                            OcRunnerProtocol   protocol,
                            const char        *gateway,
                            const char        *username,
                            const char        *cookie,
                            const char        *usergroup,
                            const char        *extra_args,
                            GError           **error);

void oc_runner_disconnect (OcRunner *runner);

OcRunnerState oc_runner_get_state (OcRunner *runner);

const char *oc_runner_get_tunnel_ip4 (OcRunner *runner);
const char *oc_runner_get_tunnel_ip6 (OcRunner *runner);

GHashTable *oc_runner_get_config (OcRunner *runner);

const char *oc_runner_state_to_string (OcRunnerState state);

G_END_DECLS

#endif /* __OPENCONNECT_RUNNER_H__ */
