# GNOME VPN SSO

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)](https://www.linux.org/)
[![GNOME](https://img.shields.io/badge/GNOME-42%2B-4A86CF.svg)](https://www.gnome.org/)

A NetworkManager VPN plugin that provides native GNOME integration for OpenConnect-based VPNs with Single Sign-On (SSO) authentication. Seamlessly connect to corporate VPNs using GlobalProtect (Palo Alto) and AnyConnect (Cisco) protocols with browser-based SAML/OAuth authentication.

## Features

- **Native GNOME Integration** - Appears directly in GNOME Settings alongside OpenVPN, WireGuard, and other VPN types
- **Single Sign-On Support** - Browser-based SAML/OAuth authentication flows via Playwright
- **Multi-Protocol Support** - Works with both GlobalProtect and AnyConnect VPNs
- **Heuristic Login Automation** - Robust detection of username/password/OTP prompts
- **Visual Feedback** - Connection status visible in GNOME's network indicator
- **Profile Management** - Create and manage multiple VPN profiles
- **Enterprise Ready** - Built for corporate VPN environments with SSO requirements
- **One-Click Build** - Simple installation from source or .deb package

## Screenshots

> Screenshots coming soon

## Supported VPN Protocols

### GlobalProtect (Palo Alto Networks)
- SAML-based authentication
- Portal and gateway discovery
- Prelogin cookie handling

### AnyConnect (Cisco)
- OAuth/SAML authentication
- Multi-factor authentication

Both protocols use OpenConnect for the actual VPN tunnel establishment.

## Installation

### Prerequisites

#### Ubuntu 22.04+ / Debian 12+
```bash
sudo apt update
sudo apt install networkmanager openconnect python3-playwright python3-pyotp
PLAYWRIGHT_BROWSERS_PATH=/var/cache/ms-playwright python3 -m playwright install chromium
```

### Option 1: Install from .deb Package

Download the latest release and install:

```bash
sudo dpkg -i gnome-vpn-sso_*.deb
sudo apt-get install -f  # Install any missing dependencies
```

After installation, restart NetworkManager:

```bash
sudo systemctl restart NetworkManager
```

### Option 2: Build from Source

#### Install Build Dependencies

**Ubuntu/Debian:**
```bash
sudo apt install meson ninja-build gcc \
    libnm-dev libgtk-4-dev libadwaita-1-dev \
    libwebkitgtk-6.0-dev libsecret-1-dev \
    python3-dev python3-pip python3-gi python3-playwright python3-pyotp \
    gir1.2-gtk-4.0 openconnect git
```

**Fedora:**
```bash
sudo dnf install meson ninja-build gcc \
    NetworkManager-libnm-devel gtk4-devel libadwaita-devel \
    webkitgtk6.0-devel libsecret-devel \
    python3-devel python3-pip python3-gobject python3-playwright python3-pyotp \
    openconnect git
```

#### Quick Build

```bash
git clone https://github.com/FHNW-Security-Lab/Gnome-VPN.git
cd Gnome-VPN
./build.sh
sudo meson install -C builddir
```

#### Manual Build

```bash
# Download SSO backend dependencies
./build.sh --deps-only

# Configure build
meson setup builddir

# Compile
meson compile -C builddir

# Install
sudo meson install -C builddir

# Restart NetworkManager
sudo systemctl restart NetworkManager
```

#### Build .deb Package

```bash
./package-deb.sh
# Output: gnome-vpn-sso_<version>_amd64.deb

### Advanced options

- Force headless auth (no UI):
  ```bash
  nmcli connection modify "My VPN" vpn.data.headless true
  ```

### Nix / NixOS

#### Development shell
```bash
nix-shell
```

#### Package build
```bash
nix build .#gnome-vpn-sso
```

#### NixOS module
```nix
{
  imports = [ ./nix/nixos-module.nix ];

  services.gnome-vpn-sso.enable = true;
}
```

This module also sets a tmpfiles rule for Playwright browsers at
`/var/cache/ms-playwright`.
```

## Quick Start

### Adding a VPN Connection

1. Open **GNOME Settings** → **Network**
2. Click the **+** button next to **VPN**
3. Select **"SSO VPN (GlobalProtect/AnyConnect)"** from the list
4. Fill in the connection details:
   - **Name**: A friendly name for your VPN (e.g., "Work VPN")
   - **Gateway**: Your VPN server address (e.g., `vpn.company.com`)
   - **Protocol**: Choose `GlobalProtect` or `AnyConnect`
   - **Username**: (Optional) Your username for display purposes
5. Click **Add** to save the profile

### Connecting to VPN

1. Click the **network icon** in the top-right corner of your screen
2. Under VPN connections, click your **VPN profile name**
3. A browser window will open automatically for SSO authentication
4. **Sign in** using your corporate credentials
5. Complete any multi-factor authentication if required
6. The browser will close automatically once authenticated
7. Your VPN connection will establish
8. A **VPN lock icon** appears in the network indicator when connected

### Disconnecting

1. Click the **network icon** in the top panel
2. Click your active **VPN connection**
3. Select **Disconnect**

## Configuration

### Basic Settings

- **Gateway**: The VPN server hostname or IP address
  - Examples: `vpn.company.com`, `gp.example.org`
- **Protocol**: Select the appropriate protocol for your VPN
  - `GlobalProtect` for Palo Alto Networks
  - `AnyConnect` for Cisco VPNs
- **Username**: Optional display username

### Advanced Settings

Click **Advanced** in the VPN configuration dialog to access:

- **User Group**: Specify portal and prelogin-cookie settings
  - Format: `portal:prelogin-cookie`
- **Custom OpenConnect Arguments**: Additional command-line options
  - Example: `--os=linux-64 --servercert pin-sha256:ABC123...`
  - See `man openconnect` for available options

### Configuration File Location

VPN profiles are stored by NetworkManager in:
```
/etc/NetworkManager/system-connections/
```

## Troubleshooting

### VPN Plugin Not Showing in Settings

1. Verify installation:
   ```bash
   ls /usr/lib/NetworkManager/VPN/
   # Should show: nm-gnome-vpn-sso-service.name
   ```

2. Restart NetworkManager:
   ```bash
   sudo systemctl restart NetworkManager
   ```

3. Check GNOME Settings plugins:
   ```bash
   ls /usr/lib/*/gnome-control-center/
   # Should show: libnm-vpn-plugin-gnome-vpn-sso.so
   ```

### Connection Fails Immediately

1. Check NetworkManager logs:
   ```bash
   journalctl -u NetworkManager -f
   ```

2. Verify OpenConnect is installed:
   ```bash
   which openconnect
   openconnect --version
   ```

3. Test connectivity to VPN gateway:
   ```bash
   ping vpn.company.com
   ```

### SSO Browser Window Doesn't Open

1. Verify Playwright is installed and Chromium is present:
   ```bash
   python3 -m playwright --version
   PLAYWRIGHT_BROWSERS_PATH=/var/cache/ms-playwright python3 -m playwright install chromium
   ```

2. Ensure the browser cache path is writable by the service:
   ```bash
   sudo mkdir -p /var/cache/ms-playwright /var/cache/gnome-vpn-sso
   sudo chmod 755 /var/cache/ms-playwright /var/cache/gnome-vpn-sso
   ```

3. If you saved Password/TOTP, the SSO flow runs headless.
   - To force a visible browser window: `nmcli connection modify <NAME> +vpn.data headless=false`
   - Or clear secrets in the VPN editor.

### Authentication Succeeds but Tunnel Fails

1. Check OpenConnect can establish tunnel:
   ```bash
   sudo openconnect --protocol=gp vpn.company.com
   # Or for AnyConnect:
   sudo openconnect vpn.company.com
   ```

2. Check for firewall/routing issues:
   ```bash
   sudo iptables -L -n
   ip route show
   ```

### Certificate Errors

If you encounter SSL/TLS certificate errors:

1. Accept server certificate (if trusted):
   - Add to Advanced settings: `--servercert sha256:<fingerprint>`

2. Get certificate fingerprint:
   ```bash
   openconnect --authenticate vpn.company.com
   ```

### Viewing Debug Logs

Enable debug logging:

```bash
# Stop NetworkManager
sudo systemctl stop NetworkManager

# Run with debug output
sudo NetworkManager --debug 2>&1 | tee /tmp/nm-debug.log

# In another terminal, attempt VPN connection
# Then check /tmp/nm-debug.log
```

### Common Issues

| Issue | Solution |
|-------|----------|
| "VPN service failed to start" | Check `journalctl -xe` for errors, verify dependencies installed |
| Authentication loops infinitely | Clear saved credentials, try manual authentication |
| DNS doesn't work over VPN | Check `/etc/resolv.conf`, may need to configure DNS servers |
| Connection drops frequently | Check network stability, try different gateway if available |

## Contributing

Contributions are welcome! Please follow these guidelines:

### Reporting Bugs

1. Check existing issues first
2. Include the following information:
   - Linux distribution and version
   - GNOME version (`gnome-shell --version`)
   - NetworkManager version (`nmcli --version`)
   - VPN protocol (GlobalProtect or AnyConnect)
   - Relevant logs from `journalctl -u NetworkManager`

### Submitting Pull Requests

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Follow GNOME coding style for C code
4. Test your changes thoroughly
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

### Development Setup

For development, clone the repository and install build dependencies as shown above. Use `meson setup builddir` to configure, then `meson compile -C builddir` to build.

### Code Style

- **C code**: Follow [GNOME coding style](https://developer.gnome.org/programming-guidelines/stable/c-coding-style.html.en)
- **Python**: PEP 8
- **Meson**: Standard meson formatting
- Use GLib/GObject for C object model
- GTK4 + libadwaita for user interfaces

## FHNW Security Lab Projects

This project is part of the [FHNW Security Lab](https://github.com/FHNW-Security-Lab). Check out our other projects:

- **[ExploitSimulator](https://github.com/FHNW-Security-Lab/ExploitSimulator)** - A web based x86 exploit simulator
- **[ExploitSimulator-Standalone](https://github.com/FHNW-Security-Lab/ExploitSimulator-Standalone)** - Standalone x86 Emulator and Exploit Simulator
- **[ExploitationChallenge](https://github.com/FHNW-Security-Lab/ExploitationChallenge)** - Exploitation Challenges for Teaching Software Security
- **[WebSecLab](https://github.com/FHNW-Security-Lab/WebSecLab)** - WebSecLab of FHNW
- **[Proxmoxinator](https://github.com/FHNW-Security-Lab/Proxmoxinator)** - Management Tool for Cloudinit and Proxmox
- **[mcs-analyser](https://github.com/FHNW-Security-Lab/mcs-analyser)** - Binary analysis tool to simulate and visualise communication paths of multi-component systems
- **[TraceGuard](https://github.com/FHNW-Security-Lab/TraceGuard)** - Optimizing path exploration in symbolic execution using taint analysis
- **[DefectDojoUploader](https://github.com/FHNW-Security-Lab/DefectDojoUploader)** - Easy Upload to Defect Dojo
- **[Sanitizing-Checker](https://github.com/FHNW-Security-Lab/Sanitizing-Checker)** - Checking for security and sanitizing features in a binary

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

```
Copyright (C) 2024 GNOME VPN SSO Contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
```

## Credits and Acknowledgments

This project builds upon the excellent work of:

- **[Playwright](https://playwright.dev/)** by Microsoft
  - Cross-browser automation used for SAML flows
  - Licensed under Apache-2.0

- **[OpenConnect](https://www.infradead.org/openconnect/)** by David Woodhouse and contributors
  - Open-source VPN client supporting multiple protocols
  - Licensed under LGPL-2.1

- **[NetworkManager](https://networkmanager.dev/)** by the GNOME Project
  - Network connection manager for Linux
  - Licensed under GPL-2.0+

Special thanks to the GNOME and NetworkManager communities for their comprehensive documentation and VPN plugin examples.

## Resources

- **NetworkManager VPN Plugins**: [GNOME GitLab](https://gitlab.gnome.org/GNOME)
- **OpenConnect Documentation**: [https://www.infradead.org/openconnect/](https://www.infradead.org/openconnect/)
- **GNOME Human Interface Guidelines**: [https://developer.gnome.org/hig/](https://developer.gnome.org/hig/)

## Support

- **Issues**: [GitHub Issues](https://github.com/FHNW-Security-Lab/Gnome-VPN/issues)
- **Discussions**: [GitHub Discussions](https://github.com/FHNW-Security-Lab/Gnome-VPN/discussions)

---

Made with ❤️ for the GNOME community
