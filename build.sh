#!/bin/bash

# GNOME VPN SSO - Comprehensive Build Script
# This script automates the build process for GNOME VPN SSO application
# with support for downloading dependencies, building, and installing

set -e  # Exit on any error

# ============================================================================
# Configuration Variables
# ============================================================================

PROJECT_NAME="gnome-vpn-sso"
PROJECT_VERSION="0.1.0"
BUILD_DIR="builddir"
DEPS_DIR="deps"

# Color codes for output
COLOR_RESET="\033[0m"
COLOR_RED="\033[0;31m"
COLOR_GREEN="\033[0;32m"
COLOR_YELLOW="\033[0;33m"
COLOR_BLUE="\033[0;34m"
COLOR_CYAN="\033[0;36m"

# ============================================================================
# Helper Functions
# ============================================================================

# Print colored status messages
print_status() {
    local color=$1
    shift
    echo -e "${color}==>${COLOR_RESET} $*"
}

print_error() {
    print_status "$COLOR_RED" "ERROR: $*"
}

print_success() {
    print_status "$COLOR_GREEN" "$*"
}

print_info() {
    print_status "$COLOR_BLUE" "$*"
}

print_warning() {
    print_status "$COLOR_YELLOW" "WARNING: $*"
}

# ============================================================================
# Dependency Management
# ============================================================================

# Check if required dependencies are installed
check_dependencies() {
    print_info "Checking for required build dependencies..."

    local missing_deps=()
    local deps=(
        "meson"
        "ninja"
        "gcc"
        "pkg-config"
        "python3"
        "git"
        "wget"
        "unzip"
    )

    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            missing_deps+=("$dep")
        fi
    done

    # Check for library packages using pkg-config
    local lib_deps=(
        "libnm"
        "gtk4"
        "libadwaita-1"
        "libsecret-1"
        "python3"
    )

    for lib in "${lib_deps[@]}"; do
        if ! pkg-config --exists "$lib" 2>/dev/null; then
            missing_deps+=("$lib")
        fi
    done

    if [ ${#missing_deps[@]} -gt 0 ]; then
        print_warning "Missing dependencies detected:"
        for dep in "${missing_deps[@]}"; do
            echo "  - $dep"
        done
        print_info "Run with --install-build-deps to install them automatically"
        return 1
    fi

    print_success "All required dependencies are installed"
    return 0
}

# Install build dependencies using apt
install_build_deps() {
    print_info "Installing build dependencies..."

    local packages=(
        "meson"
        "ninja-build"
        "gcc"
        "g++"
        "pkg-config"
        "libnm-dev"
        "libgtk-4-dev"
        "libadwaita-1-dev"
        "libsecret-1-dev"
        "python3-dev"
        "python3-pip"
        "python3-gi"
        "python3-setuptools"
        "gir1.2-gtk-4.0"
        "gir1.2-adw-1"
        "gir1.2-nm-1.0"
        "openconnect"
        "git"
        "wget"
        "unzip"
    )

    print_info "The following packages will be installed:"
    printf '  %s\n' "${packages[@]}"

    if [ "$EUID" -eq 0 ]; then
        apt update
        apt install -y "${packages[@]}"
    else
        sudo apt update
        sudo apt install -y "${packages[@]}"
    fi

    print_success "Build dependencies installed successfully"
}

# Download SSO dependencies
download_sso_deps() {
    print_info "Downloading SSO dependencies..."

    # Create deps directory if it doesn't exist
    mkdir -p "$DEPS_DIR"
    cd "$DEPS_DIR"

    # Download gp-saml-gui
    print_info "Downloading gp-saml-gui..."
    if [ -d "gp-saml-gui" ]; then
        print_warning "gp-saml-gui directory already exists, removing..."
        rm -rf "gp-saml-gui"
    fi

    wget -O gp-saml-gui-master.zip https://github.com/dlenski/gp-saml-gui/archive/master.zip
    unzip -q gp-saml-gui-master.zip
    mv gp-saml-gui-master gp-saml-gui
    rm gp-saml-gui-master.zip
    print_success "gp-saml-gui downloaded successfully"

    # Clone openconnect-sso
    print_info "Cloning openconnect-sso..."
    if [ -d "openconnect-sso" ]; then
        print_warning "openconnect-sso directory already exists, updating..."
        cd openconnect-sso
        git pull
        cd ..
    else
        git clone https://github.com/vlaci/openconnect-sso.git
        print_success "openconnect-sso cloned successfully"
    fi

    cd ..
    print_success "All SSO dependencies downloaded to $DEPS_DIR/"
}

# ============================================================================
# Build Functions
# ============================================================================

# Build the project using Meson
build_project() {
    print_info "Building $PROJECT_NAME..."

    # Check if meson.build exists
    if [ ! -f "meson.build" ]; then
        print_error "meson.build not found in current directory"
        print_info "Please ensure you're running this script from the project root"
        exit 1
    fi

    # Setup build directory
    if [ -d "$BUILD_DIR" ]; then
        print_warning "Build directory already exists, reconfiguring..."
        meson setup --reconfigure "$BUILD_DIR"
    else
        print_info "Setting up build directory..."
        meson setup "$BUILD_DIR" --prefix=/usr/local
    fi

    # Compile the project
    print_info "Compiling..."
    meson compile -C "$BUILD_DIR"

    print_success "Build completed successfully"
    print_info "Build artifacts are in $BUILD_DIR/"
}

# Install the project
install_project() {
    print_info "Installing $PROJECT_NAME..."

    if [ ! -d "$BUILD_DIR" ]; then
        print_error "Build directory not found. Please build the project first."
        exit 1
    fi

    # Install using meson
    if [ "$EUID" -eq 0 ]; then
        meson install -C "$BUILD_DIR"
    else
        sudo meson install -C "$BUILD_DIR"
    fi

    print_success "$PROJECT_NAME installed successfully"
}

# Clean build directory
clean_build() {
    print_info "Cleaning build directory..."

    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        print_success "Build directory removed"
    else
        print_warning "Build directory does not exist"
    fi
}

# ============================================================================
# Usage and Help
# ============================================================================

show_usage() {
    cat << EOF
$PROJECT_NAME Build Script v$PROJECT_VERSION

Usage: $0 [OPTIONS]

OPTIONS:
    --deps-only              Download SSO dependencies only (gp-saml-gui, openconnect-sso)
    --install-build-deps     Install system build dependencies via apt
    --clean                  Clean the build directory
    --install                Build and install the project
    --help, -h               Show this help message

EXAMPLES:
    # Install build dependencies and build
    $0 --install-build-deps
    $0

    # Download SSO dependencies only
    $0 --deps-only

    # Clean and rebuild
    $0 --clean
    $0

    # Build and install in one command
    $0 --install

    # Full setup from scratch
    $0 --install-build-deps --deps-only --install

DEPENDENCIES:
    System packages: meson, ninja-build, gcc, libnm-dev, libgtk-4-dev,
                     libadwaita-1-dev, libsecret-1-dev, python3-dev,
                     python3-pip, python3-gi, openconnect, git, wget, unzip

    External deps: gp-saml-gui, openconnect-sso (downloaded with --deps-only)

EOF
}

# ============================================================================
# Main Script Logic
# ============================================================================

main() {
    print_info "Starting $PROJECT_NAME build process..."
    print_info "Project version: $PROJECT_VERSION"
    echo

    # Parse command line arguments
    local install_deps=false
    local download_deps=false
    local clean_only=false
    local install_after=false
    local deps_only=false

    while [[ $# -gt 0 ]]; do
        case $1 in
            --install-build-deps)
                install_deps=true
                shift
                ;;
            --deps-only)
                deps_only=true
                download_deps=true
                shift
                ;;
            --clean)
                clean_only=true
                shift
                ;;
            --install)
                install_after=true
                shift
                ;;
            --help|-h)
                show_usage
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done

    # Install build dependencies if requested
    if [ "$install_deps" = true ]; then
        install_build_deps
        echo
    fi

    # Download SSO dependencies if requested
    if [ "$download_deps" = true ]; then
        download_sso_deps
        echo

        # If --deps-only, exit here
        if [ "$deps_only" = true ]; then
            print_success "SSO dependencies downloaded successfully"
            exit 0
        fi
    fi

    # Clean build directory if requested
    if [ "$clean_only" = true ]; then
        clean_build
        echo
        exit 0
    fi

    # Check dependencies before building
    if ! check_dependencies; then
        print_error "Missing required dependencies"
        print_info "Install them with: $0 --install-build-deps"
        exit 1
    fi
    echo

    # Build the project
    build_project
    echo

    # Install if requested
    if [ "$install_after" = true ]; then
        install_project
        echo
    fi

    print_success "All operations completed successfully!"

    if [ "$install_after" != true ]; then
        print_info "To install, run: $0 --install"
        print_info "Or manually: sudo meson install -C $BUILD_DIR"
    fi
}

# Run main function with all arguments
main "$@"
