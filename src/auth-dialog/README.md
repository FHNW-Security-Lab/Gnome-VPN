# GNOME VPN SSO Authentication Dialog

## Overview

The authentication dialog (`nm-vpn-sso-auth-dialog`) is a GTK4/Libadwaita application that provides a browser-based SSO authentication interface for VPN connections. It uses WebKitGTK-6.0 to embed a web browser for SAML/OAuth authentication flows.

## Architecture

### Components

1. **NmVpnSsoAuthDialog** - Main window widget (AdwWindow)
   - Embeds WebKitGTK web view for SSO login
   - Monitors cookies and URL changes
   - Captures authentication tokens/cookies
   - Returns results to the VPN service daemon

2. **Main Application** - Entry point
   - Parses command-line arguments
   - Creates and displays the dialog
   - Outputs authentication results to stdout

### Key Features

- **Protocol Support**: GlobalProtect and AnyConnect
- **Cookie Detection**: Automatically captures authentication cookies
- **Progress Feedback**: Shows loading progress and status
- **Error Handling**: Displays user-friendly error messages
- **Clean UI**: Uses Libadwaita for modern GNOME integration

## Usage

### Command Line

```bash
nm-vpn-sso-auth-dialog --gateway vpn.example.com --protocol globalprotect
```

### Options

- `--gateway, -g URL` - **Required.** VPN gateway URL
- `--protocol, -p PROTOCOL` - VPN protocol (globalprotect or anyconnect). Default: globalprotect
- `--version, -v` - Show version information

### Output Format

On successful authentication, the dialog outputs to stdout:

```
COOKIE=<authentication-cookie>
USERNAME=<username>  # Optional, if available
```

Exit codes:
- `0` - Authentication successful
- `1` - Authentication failed or cancelled

## Implementation Details

### Authentication Flow

#### GlobalProtect

1. Load `https://<gateway>/global-protect/prelogin.esp`
2. User completes SAML authentication
3. Monitor for cookies: `authcookie`, `portal-userauthcookie`
4. Detect URL patterns: `/global-protect/`, `/ssl-vpn/`
5. Extract cookie value
6. Close dialog and output result

#### AnyConnect

1. Load `https://<gateway>/`
2. User completes SSO authentication
3. Monitor for cookies: `webvpn`, `webvpnlogin`
4. Detect URL patterns: `/+CSCOE+/`, `/+webvpn+/`
5. Extract cookie value
6. Close dialog and output result

### WebKit Configuration

- JavaScript: Enabled (required for SSO)
- WebGL: Disabled (security)
- Plugins: Disabled (security)
- Cookies: Accept all (required for authentication)
- User Agent: Standard Chrome/Linux UA

### UI Structure

```
AdwWindow
└── AdwToolbarView
    ├── AdwHeaderBar (top bar)
    │   └── Cancel Button
    ├── GtkProgressBar (loading indicator)
    └── Content Area
        ├── AdwStatusPage (initial loading / errors)
        │   └── GtkSpinner
        └── WebKitWebView (SSO login page)
```

### Cookie Detection

The dialog monitors cookies using WebKit's cookie manager:

1. **Load Event Monitoring**: Checks cookies on page load/redirect
2. **Cookie Manager API**: Uses async `webkit_cookie_manager_get_cookies()`
3. **Protocol-Specific Detection**:
   - GlobalProtect: `authcookie`, `portal-userauthcookie`
   - AnyConnect: `webvpn`, `webvpnlogin`
4. **Automatic Completion**: Dialog closes when cookie is captured

## Dependencies

### Build-Time
- GTK 4.10+
- Libadwaita 1.4+
- WebKitGTK-6.0 2.40+
- GLib 2.70+

### Runtime
- Same as build-time dependencies
- Working network connection
- Valid VPN gateway URL

## Integration with VPN Service

The VPN service daemon (`nm-gnome-vpn-sso-service`) spawns this dialog:

1. Service receives connection request
2. Service spawns `nm-vpn-sso-auth-dialog` with gateway/protocol
3. Dialog displays SSO login page
4. User authenticates
5. Dialog outputs cookie to stdout
6. Service captures cookie and passes to openconnect
7. VPN tunnel is established

## Security Considerations

1. **Cookie Isolation**: Each dialog instance uses a new WebKit context
2. **No Persistence**: Cookies are not saved to disk
3. **HTTPS Only**: All authentication URLs use HTTPS
4. **Minimal Features**: WebGL and plugins disabled
5. **Session Scoped**: Authentication state cleared when dialog closes

## Debugging

Enable debug output:

```bash
G_MESSAGES_DEBUG=all nm-vpn-sso-auth-dialog --gateway vpn.example.com --protocol globalprotect
```

Debug output includes:
- Page load events and URLs
- Cookie detection attempts
- Authentication completion status
- Error messages

## Testing

### Manual Testing

```bash
# GlobalProtect
./nm-vpn-sso-auth-dialog --gateway vpn.unibas.ch --protocol globalprotect

# AnyConnect
./nm-vpn-sso-auth-dialog --gateway vpn.example.com --protocol anyconnect
```

### Expected Behavior

1. Window opens with spinner and "Authenticating" message
2. Browser loads SSO login page
3. Progress bar shows loading progress
4. User completes authentication
5. Dialog automatically closes
6. Cookie printed to stdout

## Error Handling

Common errors:

- **Network Failure**: Shows error status page with message
- **Invalid Gateway**: Shows error status page
- **User Cancellation**: Exits with code 1
- **Load Timeout**: WebKit default timeout applies

All errors are displayed in the UI using AdwStatusPage with appropriate icons and messages.

## Future Enhancements

Potential improvements:
- Username extraction from SAML assertions
- Multi-factor authentication support
- Session caching (with security considerations)
- Certificate validation options
- Custom certificate support
- Kerberos/GSSAPI integration

## Files

- `main.c` - Application entry point and command-line parsing
- `nm-vpn-sso-auth-dialog.c` - Dialog implementation
- `nm-vpn-sso-auth-dialog.h` - Dialog public API
- `meson.build` - Build configuration
- `README.md` - This file

## License

GPL-3.0-or-later

## Contributors

GNOME VPN SSO Contributors
