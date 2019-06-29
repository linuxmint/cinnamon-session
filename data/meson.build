
foreach size: [ '16x16', '22x22', '24x24', '32x32', '48x48', 'scalable', ]
  filename = 'cinnamon-session-properties.' + (size == 'scalable' ? 'svg' : 'png')

  install_data(join_paths('icons', size, filename),
    install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', size, 'apps')
  )
endforeach

install_data([
    'csm-inhibit-dialog.glade',
    'hardware-compatibility',
  ],
  install_dir: join_paths(get_option('datadir'), meson.project_name())
)

install_data(
  configure_file(
    input : 'org.cinnamon.SessionManager.gschema.xml.in',
    output: 'org.cinnamon.SessionManager.gschema.xml',
    configuration: configuration_data(),
  ),
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas')
)

# Re-compile gsettings
meson.add_install_script('meson_install_schemas.py')
