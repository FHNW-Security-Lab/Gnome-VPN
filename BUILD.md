# Building gnome-vpn-sso

## Build System

This project uses the Meson build system.

## Dependencies

### Build-time Requirements

- meson >= 0.59.0
- pkg-config
- C compiler (gcc or clang)

### Library Dependencies

- glib-2.0 >= 2.70
- gio-2.0 >= 2.70
- gtk4 >= 4.10
- libnm >= 1.42
- libadwaita-1 >= 1.4
- libsecret-1 >= 0.20

### Installing Dependencies on Ubuntu/Debian

```bash
sudo apt install meson pkg-config gcc \
    libglib2.0-dev \
    libgtk-4-dev \
    libnm-dev \
    libadwaita-1-dev \
    libsecret-1-dev
```

### Installing Dependencies on Fedora

```bash
sudo dnf install meson pkgconfig gcc \
    glib2-devel \
    gtk4-devel \
    NetworkManager-libnm-devel \
    libadwaita-devel \
    libsecret-devel
```

## Building

### Configure the build

```bash
meson setup builddir
```

### Build options

You can customize the installation directories:

```bash
meson setup builddir \
    -Dnm_plugindir=/usr/lib/NetworkManager \
    -Dnm_vpn_dir=/etc/NetworkManager/VPN
```

### Compile

```bash
meson compile -C builddir
```

### Install

```bash
sudo meson install -C builddir
```

## Build Configuration

The build system creates the following components:

1. **nm-vpn-sso-service** - The VPN service daemon
   - Installed to: `${libexecdir}/nm-vpn-sso-service`

2. **nm-vpn-sso-auth-dialog** - Authentication dialog
   - Installed to: `${libexecdir}/nm-vpn-sso-auth-dialog`

3. **libnm-vpn-sso-editor.so** - NetworkManager editor plugin
   - Installed to: `${nm_plugindir}/libnm-vpn-sso-editor.so`

4. **nm-gnome-vpn-sso-service.name** - VPN service definition
   - Installed to: `${nm_vpn_dir}/nm-gnome-vpn-sso-service.name`

5. **D-Bus service file**
   - Installed to: `${datadir}/dbus-1/system-services/`

## Directory Structure

```
gnome-vpn-sso/
├── meson.build              # Main build configuration
├── meson_options.txt        # Build options
├── src/
│   ├── meson.build
│   ├── shared/              # Shared utility library
│   │   ├── meson.build
│   │   ├── utils.c
│   │   └── utils.h
│   ├── service/             # VPN service daemon
│   │   ├── meson.build
│   │   ├── nm-vpn-sso-service.c
│   │   └── nm-vpn-sso-service.h
│   ├── auth-dialog/         # Authentication dialog
│   │   ├── meson.build
│   │   ├── nm-vpn-sso-auth-dialog.c
│   │   └── nm-vpn-sso-auth-dialog.h
│   └── editor/              # Editor plugin
│       ├── meson.build
│       ├── nm-vpn-sso-editor.c
│       └── nm-vpn-sso-editor.h
└── data/
    ├── meson.build
    ├── nm-gnome-vpn-sso-service.name.in
    └── org.freedesktop.NetworkManager.vpn-sso.service.in
```

## Development

### Reconfigure build

```bash
meson setup --reconfigure builddir
```

### Clean build

```bash
meson setup --wipe builddir
```

### View build options

```bash
meson configure builddir
```

## Current Status

The build system is complete with placeholder implementations. The project structure
includes all necessary Meson build files and basic boilerplate C code that allows
Meson to configure the build (dependencies permitting).

Note: The placeholder implementations are minimal and will need to be replaced with
actual functionality for the VPN SSO plugin to work.
