
csm_marshal_sources = gnome.genmarshal(
  'csm-marshal',
  prefix: 'csm_marshal',
  sources: 'csm-marshal.list',
)

gdbus_bindings = [
  [ 'manager',    'org.gnome.SessionManager.xml'            ],
  [ 'client',     'org.gnome.SessionManager.Client.xml'     ],
  [ 'app',        'org.gnome.SessionManager.App.xml'        ],
  [ 'inhibitor',  'org.gnome.SessionManager.Inhibitor.xml'  ],
  [ 'presence',   'org.gnome.SessionManager.Presence.xml'   ],
]
gdbus_sources = []

foreach binding: gdbus_bindings
  gdbus_sources += custom_target('csm-@0@-glue.h'.format(binding[0]),
    input : binding[1],
    output: 'csm-@0@-glue.h'.format(binding[0]),
    command: [
      'dbus-binding-tool',
      '--mode=glib-server',
      '--prefix=csm_' + binding[0],
      '--output=@OUTPUT@',
      '@INPUT@',
    ]
  )
endforeach

cinnamon_session_sources = [
  'cs-idle-monitor.c',
  'csm-app.c',
  'csm-autostart-app.c',
  'csm-client.c',
  'csm-consolekit.c',
  'csm-dbus-client.c',
  'csm-fail-whale-dialog.c',
  'csm-inhibit-dialog.c',
  'csm-inhibitor.c',
  'csm-logout-dialog.c',
  'csm-manager.c',
  'csm-presence.c',
  'csm-process-helper.c',
  'csm-session-fill.c',
  'csm-session-save.c',
  'csm-store.c',
  'csm-system.c',
  'csm-systemd.c',
  'csm-util.c',
  'csm-xsmp-client.c',
  'csm-xsmp-server.c',
  'main.c',
  'mdm-log.c',
  'mdm-signal-handler.c',
  'mdm.c',
  csm_marshal_sources,
  gdbus_sources,
]

executable('cinnamon-session',
  cinnamon_session_sources,
  dependencies: [
    dbus_glib,
    execinfo,
    gconf,
    gio,
    glib,
    gtk3,
    ice,
    libcanberra,
    logind,
    sm,
    x11,
    xapp,
    xau,
    xext,
    xrender,
    xtest,
    # elogind,
  ],
  link_with: [
    libegg,
  ],
  include_directories: [ rootInclude, eggInclude, ],
  install: true,
)



# test('inhibit',
#   executable('test-inhibit',
#     'test-inhibit.c',
#   )
# )
# test('client-dbus',
#   executable('test-client-dbus',
#     'test-client-dbus.c',
#   )
# )
# test('process-helper',
#   executable('test-process-helper',
#     'test-process-helper.c',
#   )
# )
