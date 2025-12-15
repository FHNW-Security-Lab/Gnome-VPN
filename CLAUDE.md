# GNOME VPN SSO - Project Plan

## Project Overview

**gnome-vpn-sso** is a NetworkManager VPN plugin that provides native GNOME integration for OpenConnect-based VPNs with Single Sign-On (SSO) authentication. It supports both GlobalProtect (Palo Alto) and AnyConnect (Cisco) protocols.

### Goals
- Native integration into GNOME Network Settings (appears alongside OpenVPN, WireGuard, etc.)
- SSO authentication via browser-based SAML/OAuth flows
- Support for multiple VPN profiles
- Support for both GlobalProtect and AnyConnect protocols
- Visual feedback in GNOME's network indicator when connected
- One-click build and .deb package creation

### Target Platforms
- Ubuntu 22.04+ (GNOME 42+)
- Ubuntu 24.04+ (GNOME 46+)
- Debian 12+ (Bookworm, GNOME 43+)

---

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                      GNOME Network Settings                          │
│                   (gnome-control-center network)                     │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   NetworkManager VPN Plugin                          │
│              (libnm-vpn-plugin-gnome-vpn-sso.so)                    │
│                                                                      │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────┐  │
│  │ Properties      │  │ Connection      │  │ Editor Plugin       │  │
│  │ Dialog          │  │ Settings        │  │ (GNOME Settings)    │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    VPN Service Daemon                                │
│            (nm-gnome-vpn-sso-service)                               │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    Protocol Handler                          │    │
│  │  ┌──────────────────┐      ┌──────────────────────────┐     │    │
│  │  │ GlobalProtect    │      │ AnyConnect               │     │    │
│  │  │ (gp-saml-gui)    │      │ (openconnect-sso)        │     │    │
│  │  └──────────────────┘      └──────────────────────────┘     │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         OpenConnect                                  │
│                    (Actual VPN tunnel)                              │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Components

#### 1. NetworkManager VPN Plugin (`libnm-vpn-plugin-gnome-vpn-sso.so`)
- Registers with NetworkManager as a VPN provider
- Handles connection lifecycle (connect, disconnect, status)
- Communicates with GNOME Settings for configuration UI

#### 2. VPN Service Daemon (`nm-gnome-vpn-sso-service`)
- D-Bus activated service
- Orchestrates the SSO authentication flow
- Manages openconnect subprocess
- Reports connection status to NetworkManager

#### 3. Auth Dialog (`gnome-vpn-sso-auth-dialog`)
- GTK4 application for SSO authentication UI
- Embeds WebKitGTK for browser-based SSO
- Captures authentication tokens/cookies
- Returns credentials to the service daemon

#### 4. GNOME Settings Editor Plugin
- Adds "SSO VPN" option to "Add VPN" dialog
- Provides configuration UI for:
  - VPN server address
  - Protocol selection (GlobalProtect/AnyConnect)
  - Username (optional, for display)
  - Custom openconnect arguments

#### 5. SSO Backends (bundled)
- **gp-saml-gui**: For GlobalProtect SAML authentication
- **openconnect-sso**: For AnyConnect SSO authentication

---

## Directory Structure

```
gnome-vpn-sso/
├── CLAUDE.md                    # This file - project plan
├── README.md                    # User documentation
├── LICENSE                      # GPLv3
├── meson.build                  # Main build configuration
├── meson_options.txt            # Build options
│
├── build.sh                     # One-click build script
├── package-deb.sh               # Debian package builder
│
├── data/
│   ├── nm-gnome-vpn-sso-service.name.in    # NM plugin descriptor
│   ├── org.freedesktop.NetworkManager.gnome-vpn-sso.service.in  # D-Bus service
│   ├── gnome-vpn-sso.metainfo.xml          # AppStream metadata
│   └── icons/
│       └── gnome-vpn-sso.svg               # Plugin icon
│
├── src/
│   ├── service/                 # VPN service daemon
│   │   ├── meson.build
│   │   ├── main.c               # Service entry point
│   │   ├── nm-vpn-plugin.c      # NetworkManager plugin implementation
│   │   ├── nm-vpn-plugin.h
│   │   ├── sso-handler.c        # SSO authentication orchestrator
│   │   ├── sso-handler.h
│   │   ├── gp-backend.c         # GlobalProtect SSO backend
│   │   ├── gp-backend.h
│   │   ├── ac-backend.c         # AnyConnect SSO backend
│   │   ├── ac-backend.h
│   │   ├── openconnect-runner.c # OpenConnect process manager
│   │   └── openconnect-runner.h
│   │
│   ├── auth-dialog/             # Authentication dialog
│   │   ├── meson.build
│   │   ├── main.c
│   │   ├── auth-dialog.c
│   │   ├── auth-dialog.h
│   │   └── auth-dialog.ui       # GTK UI definition
│   │
│   ├── editor/                  # GNOME Settings plugin
│   │   ├── meson.build
│   │   ├── nm-vpn-editor.c      # Editor interface implementation
│   │   ├── nm-vpn-editor.h
│   │   ├── advanced-dialog.c    # Advanced settings dialog
│   │   ├── advanced-dialog.h
│   │   └── editor.ui            # GTK UI definition
│   │
│   └── shared/                  # Shared utilities
│       ├── meson.build
│       ├── vpn-config.h         # Configuration keys/constants
│       ├── utils.c
│       └── utils.h
│
├── deps/                        # Bundled dependencies
│   ├── gp-saml-gui/             # GlobalProtect SAML GUI
│   └── openconnect-sso/         # AnyConnect SSO
│
├── debian/                      # Debian packaging
│   ├── control
│   ├── rules
│   ├── changelog
│   ├── copyright
│   ├── compat
│   ├── install
│   └── gnome-vpn-sso.postinst
│
├── po/                          # Translations
│   ├── POTFILES.in
│   └── LINGUAS
│
└── tests/                       # Test suite
    ├── meson.build
    ├── test-config.c
    └── test-sso-flow.py
```

---

## Implementation Plan

### Phase 1: Project Setup & Infrastructure
**Tasks:**
- [x] Initialize git repository
- [x] Create CLAUDE.md with project plan
- [ ] Create README.md with user documentation
- [ ] Set up Meson build system
- [ ] Create basic directory structure
- [ ] Set up dependency download script
- [ ] Create .gitignore

**Files to create:**
- `meson.build`, `meson_options.txt`
- `README.md`, `LICENSE`
- `build.sh` (dependency downloader + builder)
- `.gitignore`

### Phase 2: NetworkManager Plugin Core
**Tasks:**
- [ ] Implement NM VPN plugin interface
- [ ] Create D-Bus service file
- [ ] Create .name plugin descriptor
- [ ] Implement basic connection lifecycle
- [ ] Test plugin registration with NetworkManager

**Files to create:**
- `src/service/main.c`
- `src/service/nm-vpn-plugin.c/.h`
- `data/nm-gnome-vpn-sso-service.name.in`
- `data/org.freedesktop.NetworkManager.gnome-vpn-sso.service.in`

### Phase 3: SSO Authentication Backends
**Tasks:**
- [ ] Implement GlobalProtect backend (wrapping gp-saml-gui)
- [ ] Implement AnyConnect backend (wrapping openconnect-sso)
- [ ] Create unified SSO handler interface
- [ ] Handle token/cookie capture
- [ ] Implement error handling

**Files to create:**
- `src/service/sso-handler.c/.h`
- `src/service/gp-backend.c/.h`
- `src/service/ac-backend.c/.h`

### Phase 4: OpenConnect Integration
**Tasks:**
- [ ] Implement openconnect process spawning
- [ ] Pass SSO credentials to openconnect
- [ ] Handle tunnel establishment
- [ ] Implement disconnect handling
- [ ] Parse openconnect output for status

**Files to create:**
- `src/service/openconnect-runner.c/.h`

### Phase 5: Authentication Dialog
**Tasks:**
- [ ] Create GTK4 auth dialog application
- [ ] Implement WebKitGTK integration for SSO
- [ ] Handle authentication flow UI
- [ ] Communicate with service daemon

**Files to create:**
- `src/auth-dialog/main.c`
- `src/auth-dialog/auth-dialog.c/.h`
- `src/auth-dialog/auth-dialog.ui`

### Phase 6: GNOME Settings Integration
**Tasks:**
- [ ] Implement NMVpnEditor interface
- [ ] Create configuration UI
- [ ] Implement profile settings storage
- [ ] Add advanced options dialog
- [ ] Test in GNOME Settings

**Files to create:**
- `src/editor/nm-vpn-editor.c/.h`
- `src/editor/advanced-dialog.c/.h`
- `src/editor/editor.ui`

### Phase 7: Debian Packaging
**Tasks:**
- [ ] Create debian/ control files
- [ ] Set up package dependencies
- [ ] Create postinst script for NM restart
- [ ] Test package installation
- [ ] Create package-deb.sh script

**Files to create:**
- `debian/*`
- `package-deb.sh`

### Phase 8: Testing & Polish
**Tasks:**
- [ ] Write unit tests
- [ ] Integration testing with real VPN servers
- [ ] Error handling improvements
- [ ] UI polish and translations setup
- [ ] Documentation updates

---

## Technical Details

### NetworkManager VPN Plugin API

The plugin must implement these interfaces:
- `NMVpnServicePlugin` - Main plugin interface for connection handling
- `NMVpnEditor` - Configuration editor for GNOME Settings
- `NMVpnEditorPlugin` - Plugin factory for the editor

### D-Bus Interface

Service name: `org.freedesktop.NetworkManager.gnome-vpn-sso`
Object path: `/org/freedesktop/NetworkManager/VPN/Plugin`

### Configuration Keys (stored in NetworkManager connection)

```
[vpn]
service-type=org.freedesktop.NetworkManager.gnome-vpn-sso

[vpn-data]
gateway=vpn.example.com
protocol=globalprotect|anyconnect
username=user@example.com
usergroup=portal:prelogin-cookie
extra-args=--os=linux-64
```

### SSO Flow

#### GlobalProtect
1. User activates VPN connection
2. Service daemon spawns `gp-saml-gui --portal <gateway>`
3. Browser window opens for SAML authentication
4. gp-saml-gui outputs prelogin-cookie on success
5. Service daemon spawns openconnect with cookie
6. Tunnel established, status reported to NM

#### AnyConnect
1. User activates VPN connection
2. Service daemon spawns `openconnect-sso --server <gateway>`
3. Browser window opens for SSO authentication
4. openconnect-sso establishes connection directly
5. Service monitors connection status
6. Status reported to NM

---

## SSO Backend Command Reference

### GlobalProtect (gp-saml-gui)

**Installation:**
```bash
sudo apt install python3-pip python3-gi gir1.2-gtk-3.0 gir1.2-webkit2-4.1
pip3 install https://github.com/dlenski/gp-saml-gui/archive/master.zip
```

**Usage - Get SAML Cookie:**
```bash
gp-saml-gui --portal vpn.example.com -- --protocol=gp
```

**Usage - Connect with Cookie:**
```bash
echo "<PRELOGIN_COOKIE>" | \
    sudo openconnect --protocol=gp \
    '--useragent=PAN GlobalProtect' \
    --user=user@example.com \
    --os=linux-64 \
    --usergroup=portal:prelogin-cookie \
    --passwd-on-stdin \
    vpn.example.com
```

**Full Example (Unibas):**
```bash
# Step 1: Get cookie via SAML
gp-saml-gui --portal vpn.unibas.ch -- --protocol=gp

# Step 2: Connect with the cookie
echo "fMHVE8vMZsyijPWLLazNGT8WXqcak/IOJYmI+NgNqLiibV2S8+UaySEChI71go5pT7z/zA==" | \
    sudo openconnect --protocol=gp \
    '--useragent=PAN GlobalProtect' \
    --user=christopher.scherb@unibas.ch \
    --os=linux-64 \
    --usergroup=portal:prelogin-cookie \
    --passwd-on-stdin \
    vpn.unibas.ch --protocol=gp
```

### AnyConnect (openconnect-sso)

**Installation:**
```bash
git clone https://github.com/PrestonHager/openconnect-sso.git
cd openconnect-sso
python3 -m venv .venv
source .venv/bin/activate
pip install .
```

**Usage - Connect (handles SSO and connection):**
```bash
openconnect-sso --server vpn.example.com
```

**Full Example (FHNW):**
```bash
openconnect-sso --server vpn.fhnw.ch
```

**Note:** openconnect-sso handles both the SSO authentication AND the openconnect tunnel establishment in one step, unlike gp-saml-gui which only handles authentication.

### Dependencies

**Build dependencies:**
- meson >= 0.59
- ninja-build
- gcc/g++
- libnm-dev (>= 1.2)
- libgtk-4-dev
- libadwaita-1-dev
- libwebkitgtk-6.0-dev
- libsecret-1-dev
- python3-dev
- python3-pip
- python3-gi
- gir1.2-gtk-4.0
- gir1.2-webkit-6.0

**Runtime dependencies:**
- networkmanager (>= 1.2)
- openconnect
- python3
- python3-gi
- gir1.2-gtk-3.0 (for gp-saml-gui)
- gir1.2-webkit2-4.1 (for gp-saml-gui)
- libsecret-1-0

---

## Build Instructions

### Quick Build
```bash
./build.sh
```

### Manual Build
```bash
# Install dependencies
sudo apt install meson ninja-build gcc libnm-dev libgtk-4-dev \
    libadwaita-1-dev libwebkitgtk-6.0-dev libsecret-1-dev \
    python3-dev python3-pip python3-gi gir1.2-gtk-4.0 openconnect

# Download SSO backends
./build.sh --deps-only

# Build
meson setup builddir
meson compile -C builddir

# Install
sudo meson install -C builddir
```

### Create .deb Package
```bash
./package-deb.sh
# Output: gnome-vpn-sso_<version>_amd64.deb
```

---

## Usage

### Adding a VPN Profile

1. Open **Settings** → **Network**
2. Click **+** next to VPN
3. Select **"SSO VPN (GlobalProtect/AnyConnect)"**
4. Enter:
   - **Name**: Display name for the connection
   - **Gateway**: VPN server address (e.g., `vpn.example.com`)
   - **Protocol**: GlobalProtect or AnyConnect
   - **Username**: (Optional) Your username for display
5. Click **Add**

### Connecting

1. Click the network icon in the top panel
2. Select your VPN connection
3. A browser window will open for SSO authentication
4. Complete authentication
5. VPN connects automatically
6. The network icon shows VPN status

### Disconnecting

1. Click the network icon
2. Click the active VPN connection
3. Select **Disconnect**

---

## Development Notes

### Subagent Assignment

| Phase | Subagent | Responsibility |
|-------|----------|----------------|
| 1 | general-purpose | Project setup, build system |
| 2 | general-purpose | C code for NM plugin core |
| 3 | general-purpose | SSO backend implementation |
| 4 | general-purpose | OpenConnect integration |
| 5 | general-purpose | GTK auth dialog |
| 6 | general-purpose | GNOME Settings editor |
| 7 | general-purpose | Debian packaging |
| 8 | general-purpose | Testing |

### Coding Standards
- C code: Follow GNOME coding style
- Use GLib/GObject for object model
- GTK4 + libadwaita for UI
- Meson for build system
- Python 3.10+ for helper scripts

### Testing Checklist
- [ ] Plugin appears in GNOME Settings
- [ ] Can create new VPN profile
- [ ] Can edit existing profile
- [ ] GlobalProtect SSO works
- [ ] AnyConnect SSO works
- [ ] Connection status shows in panel
- [ ] Disconnect works
- [ ] Error messages display correctly
- [ ] .deb package installs correctly
- [ ] Plugin survives NM restart

---

## References

- [NetworkManager VPN Plugin Development](https://developer.gnome.org/NetworkManager/stable/nm-vpn-dbus-types.html)
- [network-manager-openconnect source](https://gitlab.gnome.org/GNOME/NetworkManager-openconnect)
- [gp-saml-gui](https://github.com/dlenski/gp-saml-gui)
- [openconnect-sso](https://github.com/vlaci/openconnect-sso)
- [GNOME Human Interface Guidelines](https://developer.gnome.org/hig/)
- [GTK4 Documentation](https://docs.gtk.org/gtk4/)
