
executable('cinnamon-session-quit',
  'cinnamon-session-quit.c',
  dependencies: [
    gtk3,
    dbus_glib,
    gconf,
    sm,
    ice,
  ],
  include_directories: rootInclude,
  install: true,
  install_dir: get_option('bindir'),
)

executable('cinnamon-session-check-accelerated',
  'cinnamon-session-check-accelerated.c',
  dependencies: [
    gtk3,
    x11,
  ],
  include_directories: rootInclude,
  install: true,
  install_dir: get_option('libexecdir'),
)

executable('cinnamon-session-check-accelerated-helper',
  'cinnamon-session-check-accelerated-helper.c',
  dependencies: [
    gl,
    x11,
    xcomposite,
  ],
  include_directories: rootInclude,
  install: true,
  install_dir: get_option('libexecdir'),
)
