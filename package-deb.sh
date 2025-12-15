#!/bin/bash
set -e

# Build Debian package for gnome-vpn-sso
# This script builds the .deb package using dpkg-buildpackage

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "Building gnome-vpn-sso Debian package"
echo "=========================================="
echo ""

# Check if required tools are installed
check_dependencies() {
    local missing_deps=()

    for cmd in dpkg-buildpackage debuild; do
        if ! command -v "$cmd" &> /dev/null; then
            missing_deps+=("$cmd")
        fi
    done

    if [ ${#missing_deps[@]} -gt 0 ]; then
        echo "ERROR: Missing required dependencies:"
        printf '  - %s\n' "${missing_deps[@]}"
        echo ""
        echo "Install them with:"
        echo "  sudo apt-get install build-essential devscripts debhelper"
        exit 1
    fi
}

# Clean previous builds
clean_previous_builds() {
    echo "Cleaning previous builds..."
    rm -f ../*.deb ../*.changes ../*.buildinfo ../*.dsc
    rm -rf debian/.debhelper debian/gnome-vpn-sso debian/files debian/debhelper-build-stamp
    rm -rf obj-x86_64-linux-gnu
    echo "Done."
    echo ""
}

# Build the package
build_package() {
    echo "Building package..."
    echo ""

    # Build unsigned package (for local installation)
    dpkg-buildpackage -us -uc -b

    echo ""
    echo "Build complete!"
}

# Move .deb to project root
move_package() {
    echo ""
    echo "Moving .deb package to project root..."

    DEB_FILE=$(ls -t ../*.deb 2>/dev/null | head -n1)

    if [ -n "$DEB_FILE" ]; then
        mv "$DEB_FILE" .
        echo "Package moved: $(basename "$DEB_FILE")"
        echo ""
        echo "=========================================="
        echo "SUCCESS! Package ready for installation:"
        echo "  $(pwd)/$(basename "$DEB_FILE")"
        echo ""
        echo "Install with:"
        echo "  sudo dpkg -i $(basename "$DEB_FILE")"
        echo "  sudo apt-get install -f  # if there are dependency issues"
        echo "=========================================="
    else
        echo "ERROR: No .deb file found!"
        exit 1
    fi
}

# Main execution
main() {
    check_dependencies
    clean_previous_builds
    build_package
    move_package
}

main "$@"
