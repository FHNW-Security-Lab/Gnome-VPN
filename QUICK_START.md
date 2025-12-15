# Quick Start: Building GNOME VPN SSO

## TL;DR - One Command Install

```bash
sudo apt install libgtk-4-dev libnm-dev libadwaita-1-dev libsecret-1-dev && \
meson setup builddir --wipe && \
meson compile -C builddir
```

---

## Step-by-Step Instructions

### 1. Install Build Dependencies

```bash
sudo apt install libgtk-4-dev libnm-dev libadwaita-1-dev libsecret-1-dev
```

### 2. Configure Build

```bash
meson setup builddir --wipe
```

Expected output:
```
Project name: gnome-vpn-sso
Project version: 0.1.0
Dependencies found: glib-2.0, gio-2.0, gtk4, libnm, libadwaita-1, libsecret-1
Build targets: 4 (2 executables, 1 shared module, 1 static library)
```

### 3. Compile

```bash
meson compile -C builddir
```

### 4. Install (Optional)

```bash
sudo meson install -C builddir
```

This installs:
- `/usr/local/libexec/nm-vpn-sso-service` (VPN service daemon)
- `/usr/local/libexec/nm-vpn-sso-auth-dialog` (Auth dialog)
- `/usr/local/lib/x86_64-linux-gnu/NetworkManager/libnm-vpn-sso-editor.so` (Editor plugin)
- `/usr/local/etc/NetworkManager/VPN/nm-gnome-vpn-sso-service.name` (NM descriptor)
- `/usr/local/share/dbus-1/system-services/org.freedesktop.NetworkManager.vpn-sso.service` (D-Bus service)

### 5. Restart NetworkManager

```bash
sudo systemctl restart NetworkManager
```

---

## Current Build Status

✅ **Build System**: Working (Meson 1.7.0)  
✅ **Source Files**: All present  
✅ **Configuration**: Valid  
❌ **Dependencies**: 4 development packages missing

**Missing packages:**
- libgtk-4-dev
- libnm-dev  
- libadwaita-1-dev
- libsecret-1-dev

---

## What Gets Built

| Target | Type | Output | Purpose |
|--------|------|--------|---------|
| libvpn-sso-shared.a | Static Lib | Internal | Shared utilities |
| nm-vpn-sso-service | Executable | `/usr/local/libexec/` | VPN service daemon |
| nm-vpn-sso-auth-dialog | Executable | `/usr/local/libexec/` | SSO authentication UI |
| libnm-vpn-sso-editor.so | Shared Module | `${libdir}/NetworkManager/` | GNOME Settings plugin |

---

## Troubleshooting

### Build fails with "gtk4 not found"
**Solution**: Install `libgtk-4-dev`

### Build fails with "libnm not found"
**Solution**: Install `libnm-dev`

### Build fails with "libadwaita-1 not found"
**Solution**: Install `libadwaita-1-dev`

### Build fails with "libsecret-1 not found"
**Solution**: Install `libsecret-1-dev`

### Plugin doesn't appear in GNOME Settings
**Solutions**:
1. Ensure installation succeeded: `ls -la /usr/local/lib/*/NetworkManager/`
2. Restart NetworkManager: `sudo systemctl restart NetworkManager`
3. Check D-Bus service: `systemctl --user status org.freedesktop.NetworkManager.vpn-sso`

---

## Build Verification

After successful build, verify with:

```bash
# Check build outputs
ls -lh builddir/src/service/nm-vpn-sso-service
ls -lh builddir/src/auth-dialog/nm-vpn-sso-auth-dialog
ls -lh builddir/src/editor/libnm-vpn-sso-editor.so
ls -lh builddir/src/shared/libvpn-sso-shared.a

# Check size and permissions
file builddir/src/service/nm-vpn-sso-service
file builddir/src/editor/libnm-vpn-sso-editor.so
```

---

## More Information

- **Full Build Report**: See `BUILD_REPORT.md`
- **Project Plan**: See `CLAUDE.md`
- **User Documentation**: See `README.md`

