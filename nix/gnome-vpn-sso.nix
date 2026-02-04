{ lib
, python3Packages
, meson
, ninja
, pkg-config
, wrapGAppsHook4
, networkmanager
, gtk4
, glib
, libadwaita
, libsecret
, webkitgtk_6_0
, openconnect
, vpnc-scripts
, writeShellScriptBin
, iproute2
, procps
, playwright-driver
}:

let
  openconnectWrapped = writeShellScriptBin "openconnect" ''
    exec ${lib.getExe openconnect} --script ${lib.getExe' vpnc-scripts "vpnc-script"} "$@"
  '';
in
python3Packages.buildPythonApplication rec {
  pname = "gnome-vpn-sso";
  version = "0.1.0";
  format = "other";

  src = lib.cleanSource ../.;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wrapGAppsHook4
  ];

  buildInputs = [
    networkmanager
    gtk4
    glib
    libadwaita
    libsecret
    webkitgtk_6_0
    playwright-driver
  ];

  pythonPath = with python3Packages; [
    playwright
    pyotp
  ];

  mesonFlags = [
    "-Dnm_plugindir=${placeholder "out"}/lib/NetworkManager"
    "-Dnm_vpn_dir=${placeholder "out"}/lib/NetworkManager/VPN"
  ];

  PKG_CONFIG_LIBNM_VPNSERVICEDIR = "${placeholder "out"}/lib/NetworkManager/VPN";

  dontWrapGApps = true;
  preFixup = ''
    makeWrapperArgs+=("''${gappsWrapperArgs[@]}")
  '';

  postFixup = ''
    wrapPythonProgramsIn "$out/libexec/gnome-vpn-sso" "$out $pythonPath"
    wrapProgram $out/libexec/nm-vpn-sso-service \
      --prefix PATH : ${lib.makeBinPath [ openconnectWrapped openconnect iproute2 procps ]} \
      --set PLAYWRIGHT_BROWSERS_PATH ${playwright-driver.browsers}
  '';

  postInstall = ''
    substituteInPlace $out/lib/NetworkManager/VPN/nm-gnome-vpn-sso-service.name \
      --replace /usr/libexec "$out/libexec" \
      --replace "plugin=libnm-vpn-plugin-vpn-sso-editor.so" \
        "plugin=$out/lib/NetworkManager/libnm-vpn-plugin-vpn-sso-editor.so"
  '';

  passthru = {
    networkManagerPlugin = "VPN/nm-gnome-vpn-sso-service.name";
    networkManagerRuntimeDeps = [ openconnect vpnc-scripts iproute2 procps ];
    networkManagerTmpfilesRules = [
      "L+ /var/cache/ms-playwright - - - - ${playwright-driver.browsers}"
      "d /var/cache/gnome-vpn-sso 0755 root root -"
    ];
  };

  doCheck = false;

  meta = with lib; {
    description = "NetworkManager VPN plugin for SSO-based OpenConnect (AnyConnect and GlobalProtect)";
    homepage = "https://github.com/FHNW-Security-Lab/Gnome-VPN";
    license = licenses.gpl3Plus;
    platforms = platforms.linux;
  };
}
