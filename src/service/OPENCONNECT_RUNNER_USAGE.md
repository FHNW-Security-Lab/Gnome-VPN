# OpenConnect Runner Usage Guide

## Overview

The `OcRunner` component manages OpenConnect VPN connections for the gnome-vpn-sso service. It handles process spawning, output monitoring, credential passing, and state reporting.

## Architecture

```
┌─────────────────────────────────────────────────┐
│           VPN Service Daemon                     │
│         (nm-vpn-sso-service)                    │
│                                                  │
│  ┌────────────────────────────────────────────┐ │
│  │         OcRunner                            │ │
│  │                                             │ │
│  │  • Spawns openconnect process              │ │
│  │  • Monitors stdout/stderr                  │ │
│  │  • Parses connection status                │ │
│  │  • Reports state changes via signals       │ │
│  │  • Manages graceful disconnect             │ │
│  └────────────────────────────────────────────┘ │
│                     │                            │
└─────────────────────┼────────────────────────────┘
                      │
                      ▼
         ┌────────────────────────┐
         │    OpenConnect         │
         │  (VPN tunnel process)  │
         └────────────────────────┘
```

## API Reference

### Types

#### OcRunnerState

Connection state enumeration:

```c
typedef enum {
    OC_RUNNER_STATE_IDLE,           // Not connected
    OC_RUNNER_STATE_STARTING,       // Initializing connection
    OC_RUNNER_STATE_AUTHENTICATING, // Performing authentication
    OC_RUNNER_STATE_CONNECTING,     // Establishing tunnel
    OC_RUNNER_STATE_CONNECTED,      // Tunnel active
    OC_RUNNER_STATE_DISCONNECTING,  // Closing connection
    OC_RUNNER_STATE_FAILED          // Connection failed
} OcRunnerState;
```

#### OcRunnerProtocol

Supported VPN protocols:

```c
typedef enum {
    OC_RUNNER_PROTOCOL_GLOBALPROTECT,  // Palo Alto GlobalProtect
    OC_RUNNER_PROTOCOL_ANYCONNECT      // Cisco AnyConnect
} OcRunnerProtocol;
```

### Functions

#### oc_runner_new()

```c
OcRunner *oc_runner_new (void);
```

Creates a new OcRunner instance.

**Returns:** A new `OcRunner` object (transfer full)

---

#### oc_runner_connect()

```c
gboolean oc_runner_connect (OcRunner          *runner,
                            OcRunnerProtocol   protocol,
                            const char        *gateway,
                            const char        *username,
                            const char        *cookie,
                            const char        *usergroup,
                            const char        *extra_args,
                            GError           **error);
```

Starts an OpenConnect VPN connection.

**Parameters:**
- `runner`: The OcRunner instance
- `protocol`: VPN protocol to use (GlobalProtect or AnyConnect)
- `gateway`: VPN gateway hostname/IP (e.g., "vpn.example.com")
- `username`: Username for authentication (can be NULL)
- `cookie`: SSO authentication cookie (required)
- `usergroup`: User group for GlobalProtect (e.g., "portal:prelogin-cookie", can be NULL)
- `extra_args`: Additional openconnect arguments space-separated (can be NULL)
- `error`: Return location for error (can be NULL)

**Returns:** `TRUE` on success, `FALSE` on error

---

#### oc_runner_disconnect()

```c
void oc_runner_disconnect (OcRunner *runner);
```

Disconnects an active VPN connection gracefully. Sends SIGTERM first, then SIGKILL after 5 seconds if needed.

---

#### oc_runner_get_state()

```c
OcRunnerState oc_runner_get_state (OcRunner *runner);
```

Gets the current connection state.

**Returns:** Current `OcRunnerState`

---

#### oc_runner_get_tunnel_ip4()

```c
const char *oc_runner_get_tunnel_ip4 (OcRunner *runner);
```

Gets the IPv4 address assigned to the tunnel.

**Returns:** IPv4 address string or NULL if not available

---

#### oc_runner_get_tunnel_ip6()

```c
const char *oc_runner_get_tunnel_ip6 (OcRunner *runner);
```

Gets the IPv6 address assigned to the tunnel.

**Returns:** IPv6 address string or NULL if not available

---

#### oc_runner_get_config()

```c
GHashTable *oc_runner_get_config (OcRunner *runner);
```

Gets the tunnel configuration parameters (DNS servers, routes, device name, etc.).

**Returns:** Configuration hash table (transfer none)

---

### Signals

#### state-changed

```c
void user_function (OcRunner      *runner,
                    OcRunnerState  state,
                    gpointer       user_data);
```

Emitted when the connection state changes.

---

#### tunnel-ready

```c
void user_function (OcRunner      *runner,
                    const char    *ip4_address,
                    const char    *ip6_address,
                    GHashTable    *config,
                    gpointer       user_data);
```

Emitted when the VPN tunnel is established and ready for use.

**Parameters:**
- `ip4_address`: IPv4 address assigned (or NULL)
- `ip6_address`: IPv6 address assigned (or NULL)
- `config`: Configuration parameters (DNS, routes, etc.)

---

#### log-message

```c
void user_function (OcRunner      *runner,
                    const char    *message,
                    gpointer       user_data);
```

Emitted for informational log messages from OpenConnect.

---

#### error-occurred

```c
void user_function (OcRunner      *runner,
                    GError        *error,
                    gpointer       user_data);
```

Emitted when an error occurs during connection.

---

## Example Usage

### GlobalProtect Connection

```c
#include "openconnect-runner.h"

static void
on_state_changed (OcRunner *runner, OcRunnerState state, gpointer user_data)
{
    g_print ("VPN state changed to: %s\n", oc_runner_state_to_string (state));
}

static void
on_tunnel_ready (OcRunner *runner,
                 const char *ip4,
                 const char *ip6,
                 GHashTable *config,
                 gpointer user_data)
{
    g_print ("VPN tunnel ready!\n");
    if (ip4)
        g_print ("  IPv4: %s\n", ip4);
    if (ip6)
        g_print ("  IPv6: %s\n", ip6);

    // Report connection to NetworkManager
    // nm_vpn_service_plugin_set_state (plugin, NM_VPN_SERVICE_STATE_STARTED);
}

static void
on_log_message (OcRunner *runner, const char *message, gpointer user_data)
{
    g_debug ("OpenConnect: %s", message);
}

static void
on_error_occurred (OcRunner *runner, GError *error, gpointer user_data)
{
    g_warning ("VPN error: %s", error->message);
    // nm_vpn_service_plugin_failure (plugin, NM_VPN_PLUGIN_FAILURE_CONNECT_FAILED);
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    OcRunner *runner;
    GError *error = NULL;

    // Sample SSO cookie from gp-saml-gui
    const char *cookie = "fMHVE8vMZsyijPWLLazNGT8WXqcak/IOJYmI+NgNqLi...";

    loop = g_main_loop_new (NULL, FALSE);
    runner = oc_runner_new ();

    // Connect signals
    g_signal_connect (runner, "state-changed",
                     G_CALLBACK (on_state_changed), NULL);
    g_signal_connect (runner, "tunnel-ready",
                     G_CALLBACK (on_tunnel_ready), NULL);
    g_signal_connect (runner, "log-message",
                     G_CALLBACK (on_log_message), NULL);
    g_signal_connect (runner, "error-occurred",
                     G_CALLBACK (on_error_occurred), NULL);

    // Start GlobalProtect connection
    if (!oc_runner_connect (runner,
                           OC_RUNNER_PROTOCOL_GLOBALPROTECT,
                           "vpn.unibas.ch",
                           "user@unibas.ch",
                           cookie,
                           "portal:prelogin-cookie",
                           "--os=linux-64",
                           &error)) {
        g_error ("Failed to connect: %s", error->message);
        g_error_free (error);
        return 1;
    }

    g_main_loop_run (loop);

    // Cleanup
    oc_runner_disconnect (runner);
    g_object_unref (runner);
    g_main_loop_unref (loop);

    return 0;
}
```

### AnyConnect Connection

```c
// Start AnyConnect connection
if (!oc_runner_connect (runner,
                       OC_RUNNER_PROTOCOL_ANYCONNECT,
                       "vpn.example.com",
                       "user@example.com",
                       sso_cookie,
                       NULL,  // No usergroup for AnyConnect
                       NULL,  // No extra args
                       &error)) {
    g_error ("Failed to connect: %s", error->message);
}
```

## Implementation Details

### Process Management

- Uses `GSubprocess` for robust process management
- Automatically uses `pkexec` when not running as root (required for tunnel creation)
- Monitors both stdout and stderr asynchronously
- Graceful termination: SIGTERM → wait 5s → SIGKILL

### Output Parsing

The runner parses OpenConnect output to extract:

- **Connection state**: "Connected", "Established", "SSL connected", etc.
- **Tunnel IP addresses**: IPv4 and IPv6 from "Connected as X.X.X.X" messages
- **Tunnel device**: tun0, tun1, utun0, etc.
- **DNS servers**: Extracted from DNS configuration messages
- **Routes**: Split-include and split-exclude routes
- **Errors**: Failed authentication, rejected cookies, etc.

### State Machine

```
IDLE → STARTING → AUTHENTICATING → CONNECTING → CONNECTED
                                               ↓
                                         DISCONNECTING → IDLE
                                               ↓
                                            FAILED
```

### Credential Passing

The SSO cookie is written to openconnect's stdin pipe immediately after process spawn:

```c
echo "<COOKIE>\n" | openconnect --passwd-on-stdin ...
```

The stdin pipe is then closed to prevent blocking.

### GlobalProtect Command Example

```bash
pkexec openconnect \
    --protocol=gp \
    --useragent='PAN GlobalProtect' \
    --os=linux-64 \
    --user=user@example.com \
    --usergroup=portal:prelogin-cookie \
    --passwd-on-stdin \
    --non-inter \
    --reconnect-timeout=30 \
    vpn.example.com
```

### AnyConnect Command Example

```bash
pkexec openconnect \
    --protocol=anyconnect \
    --user=user@example.com \
    --passwd-on-stdin \
    --non-inter \
    --reconnect-timeout=30 \
    vpn.example.com
```

## Integration with NetworkManager

The VPN service daemon should:

1. **Create OcRunner** when connection is requested
2. **Connect signals** to handle state changes and tunnel info
3. **Call oc_runner_connect()** with SSO cookie from auth dialog
4. **Report to NetworkManager** when tunnel is ready:
   - `nm_vpn_service_plugin_set_ip4_config()` with tunnel IP/DNS/routes
   - `nm_vpn_service_plugin_set_state(NM_VPN_SERVICE_STATE_STARTED)`
5. **Call oc_runner_disconnect()** when disconnect requested
6. **Handle errors** by reporting failure to NetworkManager

## Error Handling

Common errors and handling:

| Error | Cause | Action |
|-------|-------|--------|
| Cookie rejected | Invalid/expired SSO cookie | Re-authenticate with SSO backend |
| Connection failed | Network/gateway unreachable | Report failure to NetworkManager |
| Process spawn failed | openconnect not installed | Check dependencies |
| Permission denied | pkexec rejected | Check PolicyKit configuration |

## Testing

### Manual Testing

```bash
# Build the service
meson compile -C builddir

# Run with debug output
G_MESSAGES_DEBUG=all builddir/src/service/nm-vpn-sso-service --debug

# In another terminal, trigger connection via NetworkManager
nmcli connection up "My VPN"
```

### Unit Testing

Test cases to implement:

- State transitions (idle → connected → disconnected)
- Output parsing (IP extraction, DNS parsing, route parsing)
- Error detection (authentication failures, network errors)
- Graceful shutdown (SIGTERM handling)
- Cookie passing (stdin write, pipe close)

## Security Considerations

1. **Root Privileges**: OpenConnect requires root for tunnel creation. Uses `pkexec` for privilege escalation.

2. **Cookie Storage**: SSO cookies are stored in memory only, never written to disk.

3. **Process Isolation**: OpenConnect runs in a separate process, isolated from the service daemon.

4. **Signal Handlers**: Uses proper signal handling for graceful termination.

## Debugging

Enable debug output:

```bash
export G_MESSAGES_DEBUG=all
```

Key debug messages to watch for:

- "OpenConnect process started for globalprotect/anyconnect"
- "OpenConnect state changed to: connecting/connected"
- "OpenConnect stdout: ..." (all output lines)
- "VPN tunnel ready with IP: X.X.X.X"

## Dependencies

- **GLib/GIO 2.60+**: Core libraries for GObject, GSubprocess, async I/O
- **openconnect 8.0+**: VPN client binary
- **pkexec** (optional): For non-root privilege escalation
- **sudo** (alternative): Can be used instead of pkexec

## Future Enhancements

- [ ] Reconnection logic for temporary network loss
- [ ] MTU detection and optimization
- [ ] Advanced route metrics configuration
- [ ] Proxy support (HTTP/SOCKS)
- [ ] Connection statistics (bytes transferred, duration)
- [ ] Support for client certificates
- [ ] Custom CA certificate handling

## References

- [OpenConnect Manual](https://www.infradead.org/openconnect/manual.html)
- [GSubprocess Documentation](https://docs.gtk.org/gio/class.Subprocess.html)
- [NetworkManager VPN API](https://developer.gnome.org/NetworkManager/stable/NMVpnServicePlugin.html)
