if docbook_enabled

  spec_xml_files = []

  ref_prefixes = [
    'org.gnome.SessionManager.Client',
    'org.gnome.SessionManager.ClientPrivate',
    'org.gnome.SessionManager.Inhibitor',
    'org.gnome.SessionManager.Presence',
    'org.gnome.SessionManager',
  ]

  foreach prefix: ref_prefixes
    spec_xml_files += custom_target(prefix +'.ref.xml',
      input : [
        'spec-to-docbook.xsl',
        join_paths(meson.source_root(), 'cinnamon-session', prefix + '.xml'),
      ],
      output: prefix + '.ref.xml',
      command: [
        'xsltproc',
        '@INPUT@',
      ],
      capture: true,
    )
  endforeach

  cinnamon_session_xml = configure_file(
    input : 'cinnamon-session.xml.in',
    output: 'cinnamon-session.xml',
    configuration: conf,
  )

  cinnamon_session_html = custom_target('cinnamon-session.html',
    input : [ 'config.xsl', cinnamon_session_xml, ] + spec_xml_files,
    output: 'cinnamon-session.html',
    command: [
      'xmlto',
      'xhtml-nochunks',
      '-o', meson.current_build_dir(),
      '-m', '@INPUT0@',
      '@INPUT1@',
    ],
    install: true,
    install_dir: join_paths(get_option('datadir'), 'doc', meson.project_name(), 'dbus'),
  )

endif
