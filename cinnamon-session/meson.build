
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

