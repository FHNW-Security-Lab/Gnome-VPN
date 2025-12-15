#!/bin/bash
# Apply external browser patches to openconnect-sso
# This script copies the patched files to the installed openconnect-sso package

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="/opt/gnome-vpn-sso/lib/python3.13/site-packages/openconnect_sso"

# Check if target directory exists
if [ ! -d "$TARGET_DIR" ]; then
    echo "Error: openconnect-sso not found at $TARGET_DIR"
    echo "Please install openconnect-sso first using the main build script"
    exit 1
fi

echo "Applying external browser patches to openconnect-sso..."

# Backup original files
echo "  Creating backups..."
for file in cli.py app.py authenticator.py saml_authenticator.py; do
    if [ -f "$TARGET_DIR/$file" ] && [ ! -f "$TARGET_DIR/${file}.orig" ]; then
        cp "$TARGET_DIR/$file" "$TARGET_DIR/${file}.orig"
    fi
done

# Copy patched files
echo "  Installing patched files..."
cp "$SCRIPT_DIR/cli_patch.py" "$TARGET_DIR/cli.py"
cp "$SCRIPT_DIR/app_patch.py" "$TARGET_DIR/app.py"
cp "$SCRIPT_DIR/authenticator_patch.py" "$TARGET_DIR/authenticator.py"
cp "$SCRIPT_DIR/saml_authenticator_patch.py" "$TARGET_DIR/saml_authenticator.py"

# Copy external browser module
echo "  Installing external browser module..."
cp "$SCRIPT_DIR/external_browser.py" "$TARGET_DIR/external_browser.py"

# Install additional dependencies if needed
echo "  Checking dependencies..."
if ! python3 -c "import websockets" 2>/dev/null; then
    echo "  Installing websockets..."
    /opt/gnome-vpn-sso/bin/pip install websockets 2>/dev/null || pip3 install websockets
fi

if ! python3 -c "import aiohttp" 2>/dev/null; then
    echo "  Installing aiohttp..."
    /opt/gnome-vpn-sso/bin/pip install aiohttp 2>/dev/null || pip3 install aiohttp
fi

echo "Done! External browser support has been added to openconnect-sso."
echo ""
echo "Usage: openconnect-sso --external-browser --server vpn.example.com --authenticate"
