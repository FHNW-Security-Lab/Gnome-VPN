# libnm plugin build configuration
# This minimal plugin is required for NetworkManager to recognize the VPN type

libnm_plugin_sources = files(
  'nm-vpn-plugin-vpn-sso.c',
)

libnm_plugin = shared_module(
  'nm-vpn-plugin-vpn-sso',
  sources: libnm_plugin_sources,
  dependencies: [
    glib_dep,
    gio_dep,
    libnm_dep,
  ],
  install: true,
  install_dir: nm_plugindir,
  name_prefix: 'lib',
)
