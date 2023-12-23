
csm_marshal_sources = gnome.genmarshal(
  'csm-marshal',
  prefix: 'csm_marshal',
  sources: 'csm-marshal.list',
)

gdbus_ifaces = [
  ['exported-manager', 'org.gnome.SessionManager', 'ExportedManager'],
  ['exported-client', 'org.gnome.SessionManager.Client', 'ExportedClient'],
  ['exported-client-private', 'org.gnome.SessionManager.ClientPrivate', 'ExportedClientPrivate'],
  ['exported-app', 'org.gnome.SessionManager.App', 'ExportedApp'],
  ['exported-inhibitor', 'org.gnome.SessionManager.Inhibitor', 'ExportedInhibitor'],
  ['exported-presence', 'org.gnome.SessionManager.Presence', 'ExportedPresence']
]

gdbus_sources = []

foreach iface: gdbus_ifaces
  gdbus_sources += gnome.gdbus_codegen(
    'csm-' + iface[0],
    iface[1] + '.xml',
    interface_prefix: iface[1] + '.',
    namespace: 'Csm',
    annotations: [iface[1], 'org.gtk.GDBus.C.Name', iface[2]]
  )
endforeach

cinnamon_session_sources = [
  'csm-app.c',
  'csm-autostart-app.c',
  'csm-client.c',
  'csm-consolekit.c',
  'csm-dbus-client.c',
  'csm-inhibitor.c',
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
  'inhibit-dialog-info.c',
  'main.c',
  'mdm-log.c',
  'mdm-signal-handler.c',
  'mdm.c',
  csm_marshal_sources,
  gdbus_sources,
]

executable('cinnamon-session-binary',
  cinnamon_session_sources,
  dependencies: [
    cinnamon_desktop,
    execinfo,
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
    elogind,
  ],
  include_directories: [ rootInclude ],
  install: true,
  install_dir: get_option('libexecdir'),
)

script_conf = configuration_data()
script_conf.set('libexecdir', get_option('prefix') / get_option('libexecdir'))

configure_file(
  input: 'cinnamon-session.in',
  output: 'cinnamon-session',
  install: true,
  install_dir: get_option('bindir'),
  configuration: script_conf
)

units = [
  ['test-inhibit', [], [gio, glib, gtk3]],
  ['test-client-dbus', [], [gio]],
  ['test-process-helper', files('csm-process-helper.c'), [gio]],
  ['test-session-proxy-monitor', [], [gio]]
]

foreach unit: units
  executable(
    unit[0],
    [unit[0] + '.c'] + unit[1],
    include_directories: rootInclude,
    dependencies: unit[2]
  )
endforeach

