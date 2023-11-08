csq_conf = configuration_data()

csq_conf.set_quoted('LOCALE_DIR', join_paths(get_option('prefix'), get_option('localedir')))
csq_conf.set_quoted('PACKAGE', meson.project_name())
csq_conf.set_quoted('VERSION', meson.project_version())
csq_conf.set_quoted('PKG_DATADIR', pkg_datadir)
csq_conf.set_quoted('DBUS_ADDRESS', dialog_dbus_address)

config_py = configure_file(
  output: 'config.py',
  input: 'config.py.in',
  configuration: csq_conf,
  install: true,
  install_dir: pkg_datadir,
)

install_data(
    'cinnamon-session-quit.py',
    install_dir: pkg_datadir,
    install_mode: 'rwxr-xr-x'
)

install_data(
    'cinnamon-session-quit.glade',
    install_dir: pkg_datadir
)

bin_file = configure_file(
  input : 'cinnamon-session-quit.in',
  output: 'cinnamon-session-quit',
  configuration: csq_conf,
)

install_data(
    bin_file,
    install_dir: get_option('bindir'),
    install_mode: 'rwxr-xr-x'
)