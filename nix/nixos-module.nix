{ config, lib, pkgs, ... }:

let
  cfg = config.services.gnome-vpn-sso;
in
{
  options.services.gnome-vpn-sso = {
    enable = lib.mkEnableOption "GNOME VPN SSO NetworkManager plugin";

    withOverlay = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Enable the local overlay providing the gnome-vpn-sso package.";
    };

    autoKillStale = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Kill stale nm-vpn-sso-service processes on NetworkManager restart.";
    };
  };

  config = lib.mkIf cfg.enable {
    nixpkgs.overlays = lib.optionals cfg.withOverlay [
      (import ./overlay.nix)
    ];

    networking.networkmanager.plugins = lib.mkAfter [
      pkgs.gnome-vpn-sso
    ];

    systemd.services.NetworkManager.serviceConfig.ExecStartPre =
      lib.optional cfg.autoKillStale
        "-${lib.getExe' pkgs.procps "pkill"} -KILL -f nm-vpn-sso-service";

    systemd.tmpfiles.rules = lib.optionals (pkgs.gnome-vpn-sso ? networkManagerTmpfilesRules)
      pkgs.gnome-vpn-sso.networkManagerTmpfilesRules;
  };
}
