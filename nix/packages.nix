{ pkgs ? import <nixpkgs> {} }:

{
  gnome-vpn-sso = pkgs.callPackage ./gnome-vpn-sso.nix {};
}
