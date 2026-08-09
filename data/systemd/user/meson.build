if enable_systemd
install_data(
  [
    'cinnamon-session.target',
  ],
  install_dir: systemduserunitdir,
)

# Applies to the transient scopes cinnamon creates for launched applications
install_data(
  [
    'app-override.scope.conf',
  ],
  install_dir: systemduserunitdir / 'app-cinnamon-.scope.d',
)
endif
