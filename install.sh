#!/bin/bash
# Quick install script for gnome-vpn-sso

set -e

cd "$(dirname "$0")"

echo "Installing gnome-vpn-sso..."

# Install service
sudo cp builddir/src/service/nm-vpn-sso-service /usr/libexec/

# Install Python SSO helper
sudo mkdir -p /usr/libexec/gnome-vpn-sso/core
sudo cp python/vpn-sso-auth.py /usr/libexec/gnome-vpn-sso/vpn-sso-auth
sudo cp python/core/*.py /usr/libexec/gnome-vpn-sso/core/
sudo chmod +x /usr/libexec/gnome-vpn-sso/vpn-sso-auth

# Install editor plugins
sudo cp builddir/src/editor/libnm-vpn-plugin-vpn-sso-editor.so /usr/lib/x86_64-linux-gnu/NetworkManager/
sudo cp builddir/src/editor/libnm-gtk4-vpn-plugin-vpn-sso-editor.so /usr/lib/x86_64-linux-gnu/NetworkManager/

# Install libnm plugin
sudo cp builddir/src/libnm-plugin/libnm-vpn-plugin-vpn-sso.so /usr/lib/x86_64-linux-gnu/NetworkManager/

# Install auth dialog
sudo cp builddir/src/auth-dialog/nm-vpn-sso-auth-dialog /usr/libexec/

echo "Restarting NetworkManager..."
sudo systemctl restart NetworkManager

echo "Done! You can now test the VPN connection."
