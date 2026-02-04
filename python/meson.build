# Install Python SSO helper and core modules

install_data(
  'vpn-sso-auth.py',
  install_dir: libexecdir / 'gnome-vpn-sso',
  install_mode: 'rwxr-xr-x',
)

install_data(
  'core/__init__.py',
  'core/auth.py',
  'core/totp.py',
  install_dir: libexecdir / 'gnome-vpn-sso' / 'core',
)
