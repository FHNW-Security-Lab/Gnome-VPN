# Editor plugin build configuration

editor_sources = files(
  'nm-vpn-sso-editor.c',
)

editor_headers = files(
  'nm-vpn-sso-editor.h',
)

editor_inc = include_directories('..')

# GTK4 editor plugin (modern gnome-control-center)
editor_module_gtk4 = shared_module(
  'nm-gtk4-vpn-plugin-vpn-sso-editor',
  sources: editor_sources,
  include_directories: editor_inc,
  dependencies: [
    glib_dep,
    gio_dep,
    gtk4_dep,
    libadwaita_dep,
    libnm_dep,
  ],
  c_args: [
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DLOCALEDIR="@0@"'.format(get_option('prefix') / get_option('localedir')),
  ],
  install: true,
  install_dir: nm_plugindir,
  name_prefix: 'lib',
)

# GTK3 fallback editor plugin (for compatibility)
editor_module = shared_module(
  'nm-vpn-plugin-vpn-sso-editor',
  sources: editor_sources,
  include_directories: editor_inc,
  dependencies: [
    glib_dep,
    gio_dep,
    gtk4_dep,
    libadwaita_dep,
    libnm_dep,
  ],
  c_args: [
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DLOCALEDIR="@0@"'.format(get_option('prefix') / get_option('localedir')),
  ],
  install: true,
  install_dir: nm_plugindir,
  name_prefix: 'lib',
)
