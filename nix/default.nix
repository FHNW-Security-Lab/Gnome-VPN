{ pkgs ? import <nixpkgs> {} }:

pkgs.callPackage ./gnome-vpn-sso.nix {}
