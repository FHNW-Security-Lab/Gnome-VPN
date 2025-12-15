# Data files build configuration

# Configure VPN service name file
service_name_conf = configuration_data()
service_name_conf.set('LIBEXECDIR', libexecdir)
service_name_conf.set('NM_PLUGINDIR', nm_plugindir)

configure_file(
  input: 'nm-gnome-vpn-sso-service.name.in',
  output: 'nm-gnome-vpn-sso-service.name',
  configuration: service_name_conf,
  install: true,
  install_dir: nm_vpn_dir,
)

# Configure D-Bus service file
dbus_service_conf = configuration_data()
dbus_service_conf.set('LIBEXECDIR', libexecdir)

configure_file(
  input: 'org.freedesktop.NetworkManager.vpn-sso.service.in',
  output: 'org.freedesktop.NetworkManager.vpn-sso.service',
  configuration: dbus_service_conf,
  install: true,
  install_dir: dbus_service_dir,
)

# Install D-Bus policy file
install_data(
  'nm-vpn-sso-service.conf',
  install_dir: get_option('datadir') / 'dbus-1' / 'system.d',
)

# Install icons if they exist
icon_sizes = ['16x16', '22x22', '32x32', '48x48', 'scalable']

foreach size : icon_sizes
  icon_dir = meson.project_source_root() / 'data' / 'icons' / size / 'apps'

  if size == 'scalable'
    icon_file = 'org.gnome.VpnSso.svg'
  else
    icon_file = 'org.gnome.VpnSso.png'
  endif

  icon_path = icon_dir / icon_file

  if run_command('test', '-f', icon_path, check: false).returncode() == 0
    install_data(
      icon_path,
      install_dir: icondir / size / 'apps',
      rename: icon_file,
    )
  endif
endforeach
