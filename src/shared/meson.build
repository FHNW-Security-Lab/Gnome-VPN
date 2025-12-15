# Shared library build configuration

shared_sources = files(
  'utils.c',
)

shared_headers = files(
  'utils.h',
)

shared_inc = include_directories('.')

vpn_sso_shared = static_library(
  'vpn-sso-shared',
  sources: shared_sources,
  include_directories: shared_inc,
  dependencies: [
    glib_dep,
    gio_dep,
  ],
  install: false,
)

vpn_sso_shared_dep = declare_dependency(
  link_with: vpn_sso_shared,
  include_directories: shared_inc,
  dependencies: [
    glib_dep,
    gio_dep,
  ],
)
