# Auth dialog build configuration

auth_dialog_sources = files(
  'main.c',
  'nm-vpn-sso-auth-dialog.c',
)

auth_dialog_headers = files(
  'nm-vpn-sso-auth-dialog.h',
)

# WebKitGTK-6.0 dependency (for WebKit2GTK-6.0 with GTK4)
webkit_dep = dependency('webkitgtk-6.0', version: '>= 2.40')

executable(
  'nm-vpn-sso-auth-dialog',
  sources: auth_dialog_sources,
  dependencies: [
    glib_dep,
    gio_dep,
    gtk4_dep,
    libadwaita_dep,
    libnm_dep,
    libsecret_dep,
    webkit_dep,
    vpn_sso_shared_dep,
  ],
  install: true,
  install_dir: libexecdir,
)
