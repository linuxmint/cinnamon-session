eggInclude = include_directories('.')

libegg = static_library('egg',
  [
    'eggdesktopfile.c',
    'eggsmclient-xsmp.c',
    'eggsmclient.c',
  ],
  include_directories: rootInclude,
  dependencies: [
    gtk3,
    ice,
    sm,
  ],
  c_args: [
    '-DEGG_SM_CLIENT_BACKEND_XSMP',
    # '-DEGG_SM_CLIENT_BACKEND_DBUS',
    '-DG_LOG_DOMAIN="EggSMClient"',
  ],
)
