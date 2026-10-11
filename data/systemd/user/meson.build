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

# The session bus and the D-Bus activated helpers it forks must never be an oomd candidate
install_data(
  [
    'dbus-override.service.conf',
  ],
  install_dir: systemduserunitdir / 'dbus.service.d',
)
endif
