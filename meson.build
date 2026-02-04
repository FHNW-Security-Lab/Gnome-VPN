project(
  'gnome-vpn-sso',
  'c',
  version: '0.1.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.59.0',
  default_options: [
    'warning_level=2',
    'c_std=gnu11',
  ]
)

gnome_vpn_sso_version = meson.project_version()

# Prefix and directories
prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
libdir = prefix / get_option('libdir')
libexecdir = prefix / get_option('libexecdir')
datadir = prefix / get_option('datadir')
sysconfdir = prefix / get_option('sysconfdir')
localedir = prefix / get_option('localedir')

# NetworkManager plugin and VPN directories
nm_plugindir = get_option('nm_plugindir')
if nm_plugindir == ''
  nm_plugindir = libdir / 'NetworkManager'
endif

nm_vpn_dir = get_option('nm_vpn_dir')
if nm_vpn_dir == ''
  nm_vpn_dir = prefix / 'lib' / 'NetworkManager' / 'VPN'
endif

# D-Bus service directory
dbus_service_dir = datadir / 'dbus-1' / 'system-services'

# Icon directory
icondir = datadir / 'icons' / 'hicolor'

# Build configuration
config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', gnome_vpn_sso_version)
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_h.set_quoted('LOCALEDIR', localedir)
config_h.set_quoted('NM_VPN_DIR', nm_vpn_dir)
config_h.set_quoted('NM_PLUGINDIR', nm_plugindir)
config_h.set_quoted('VPN_SSO_LIBEXECDIR', libexecdir)

configure_file(
  output: 'config.h',
  configuration: config_h
)

# Add include directories
add_project_arguments('-I' + meson.project_build_root(), language: 'c')

# Dependencies
glib_req_version = '>= 2.70'
gtk4_req_version = '>= 4.10'
libnm_req_version = '>= 1.42'
libadwaita_req_version = '>= 1.4'
libsecret_req_version = '>= 0.20'

glib_dep = dependency('glib-2.0', version: glib_req_version)
gio_dep = dependency('gio-2.0', version: glib_req_version)
gtk4_dep = dependency('gtk4', version: gtk4_req_version)
libnm_dep = dependency('libnm', version: libnm_req_version)
libadwaita_dep = dependency('libadwaita-1', version: libadwaita_req_version)
libsecret_dep = dependency('libsecret-1', version: libsecret_req_version)

# Subdirectories
subdir('src')
subdir('python')
subdir('data')

# Check for po directory
if run_command('test', '-d', meson.project_source_root() / 'po', check: false).returncode() == 0
  subdir('po')
endif

# pkg-config file
pkg = import('pkgconfig')

pkg.generate(
  name: meson.project_name(),
  description: 'GNOME VPN SSO Plugin for NetworkManager',
  version: gnome_vpn_sso_version,
  filebase: meson.project_name(),
)

# Summary
summary({
  'prefix': prefix,
  'bindir': bindir,
  'libdir': libdir,
  'libexecdir': libexecdir,
  'datadir': datadir,
  'sysconfdir': sysconfdir,
  'nm_plugindir': nm_plugindir,
  'nm_vpn_dir': nm_vpn_dir,
}, section: 'Directories')

summary({
  'glib': glib_dep.version(),
  'gio': gio_dep.version(),
  'gtk4': gtk4_dep.version(),
  'libnm': libnm_dep.version(),
  'libadwaita': libadwaita_dep.version(),
  'libsecret': libsecret_dep.version(),
}, section: 'Dependencies')
