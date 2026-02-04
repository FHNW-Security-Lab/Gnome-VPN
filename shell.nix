{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  packages = [
    pkgs.meson
    pkgs.ninja
    pkgs.pkg-config
    pkgs.gcc
    pkgs.libnm
    pkgs.gtk4
    pkgs.libadwaita
    pkgs.webkitgtk
    pkgs.libsecret
    pkgs.openconnect
    pkgs.vpnc-scripts
    pkgs.playwright-driver
    (pkgs.python3.withPackages (ps: [
      ps.playwright
      ps.pyotp
    ]))
  ];

  PLAYWRIGHT_BROWSERS_PATH = pkgs.playwright-driver.browsers;
}
