# VPN service build configuration

service_sources = files(
  'main.c',
  'nm-vpn-sso-service.c',
  'sso-handler.c',
  'gp-backend.c',
  'ac-backend.c',
  'openconnect-runner.c',
  'credential-cache.c',
)

service_headers = files(
  'nm-vpn-sso-service.h',
  'sso-handler.h',
  'gp-backend.h',
  'ac-backend.h',
  'openconnect-runner.h',
  'credential-cache.h',
)

executable(
  'nm-vpn-sso-service',
  sources: service_sources,
  dependencies: [
    glib_dep,
    gio_dep,
    libnm_dep,
    libsecret_dep,
    vpn_sso_shared_dep,
  ],
  install: true,
  install_dir: libexecdir,
)
