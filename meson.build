# https://github.com/linuxmint/cinnamon-session
project('cinnamon-session',
  [ 'c', ],
  version: '4.8.0',
  meson_version: '>=0.37.0',
)

################################################################################
gnome = import('gnome')

cc = meson.get_compiler('c')
conf = configuration_data()
project_cflags = []

if not get_option('frequent_warnings')
  warnflags = [
    '-Wno-deprecated-declarations',
    '-Wno-unused',
  ]
  foreach arg : warnflags
    if cc.has_argument(arg)
      project_cflags += arg
    endif
  endforeach
endif

################################################################################
# Dependencies

gio         = dependency('gio-2.0')
gtk3        = dependency('gtk+-3.0',    version: '>=3.0.0')
glib        = dependency('glib-2.0',    version: '>=2.37.3')
libcanberra = dependency('libcanberra')
pango       = dependency('pango')
pangoxft    = dependency('pangoxft', required: false)
sm          = dependency('sm')
ice         = dependency('ice')
x11         = dependency('x11')
xext        = dependency('xext')
xapp        = dependency('xapp', version: '>=1.0.4')
xau         = dependency('xau')
xcomposite  = dependency('xcomposite')
gl          = dependency('gl')

if get_option('gconf')
  gconf     = dependency('gconf-2.0')
else
  gconf     = dependency('', required: false)
endif
conf.set('HAVE_GCONF', gconf.found())


gio_unix    = dependency('gio-unix-2.0',      required: false)
libelogind  = dependency('libelogind',        required: false)
libsystemdl = dependency('libsystemd-login',  required: false)
if not libsystemdl.found()
  libsystemdl=dependency('libsystemd',        required: false)
endif

if gio_unix.found() and libsystemdl.found()
  logind = declare_dependency(dependencies: [ gio_unix, libsystemdl ])
else
  logind = dependency('', required: false)
endif
conf.set('HAVE_LOGIND', logind.found())

if gio_unix.found() and libelogind.found()
  elogind = declare_dependency(dependencies: [ gio_unix, libelogind ])
else
  elogind = dependency('', required: false)
endif
conf.set('HAVE_ELOGIND', elogind.found())


xtest   = dependency('xtst',    required: false)
conf.set('HAVE_XTEST', xtest.found())

xrender = dependency('xrender', required: false)
conf.set('HAVE_XRENDER', xrender.found())

docbook_enabled = get_option('docbook')
if docbook_enabled
  xmlto = find_program('xmlto')
endif
conf.set('DOCBOOK_DOCS_ENABLED', docbook_enabled)


# Execinfo support
conf.set10('HAVE_EXECINFO_H', cc.has_header('execinfo.h'))
backtrace = cc.find_library('backtrace',  required: false)
execinfo  = cc.find_library('execinfo',   required: false)


# Check for X transport interface - allows to disable ICE Transports
# See also https://bugzilla.gnome.org/show_bug.cgi?id=725100
if get_option('xtrans')
  xtrans = dependency('xtrans')
else
  xtrans = dependency('', required: false)
endif
conf.set10('HAVE_XTRANS', xtrans.found())

# Check whether IPv6 is enabled on the system...
have_ipv6 = false
ipv6_libs = []
if get_option('ipv6')
  if cc.compiles('''
    #include <sys/types.h>
    #include <sys/socket.h>
    int main() {
      socket(AF_INET6, SOCK_STREAM, 0);
      return 0;
    }
    ''')
    if cc.has_function('getaddrinfo')
      have_ipv6 = true
    else
      ipv6_test_libraries = [
        cc.find_library('bsd', required: false),
        cc.find_library('socket', required: false),
        cc.find_library('inet', required: false),
      ]
      foreach lib: ipv6_test_libraries
        if (not have_ipv6) and cc.has_function('getaddrinfo', dependencies: lib)
          have_ipv6 = true
          ipv6_libs = lib
        endif
      endforeach
    endif
  endif
endif
conf.set('ENABLE_IPV6', have_ipv6)

################################################################################
# Additionnal flags

rootInclude = include_directories('.')

conf.set_quoted('PKGDATADIR',         join_paths(get_option('prefix'), get_option('datadir'), meson.project_name()))
conf.set_quoted('LIBEXECDIR',         get_option('libexecdir'))
conf.set_quoted('LOCALE_DIR',         join_paths(get_option('prefix'), get_option('localedir')))

conf.set_quoted('PACKAGE', meson.project_name())
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())

configure_file(
  output: 'config.h',
  configuration: conf,
)

project_cflags += [
  '-DHAVE_CONFIG_H',
]

add_project_arguments(project_cflags,
  language: 'c',
)

subdir('egg')
subdir('cinnamon-session')
subdir('data')
subdir('doc')
subdir('po')
subdir('tools')

message('\n'.join([
'',
'                cinnamon-session @0@'.format(meson.project_version()),
'',
'        prefix:                   @0@'.format(get_option('prefix')),
'        exec_prefix:              @0@'.format(get_option('prefix')),
'        libdir:                   @0@'.format(get_option('libdir')),
'        libexecdir:               @0@'.format(get_option('libexecdir')),
'        bindir:                   @0@'.format(get_option('bindir')),
'        sbindir:                  @0@'.format(get_option('sbindir')),
'        sysconfdir:               @0@'.format(get_option('sysconfdir')),
'        localstatedir:            @0@'.format(get_option('localstatedir')),
'        datadir:                  @0@'.format(get_option('datadir')),
'        source code location:     @0@'.format(meson.source_root()),
'        compiler:                 @0@'.format(cc.get_id()),
'        cflags:                   @0@'.format(project_cflags),
#'        Maintainer mode:          @0@'.format(USE_MAINTAINER_MODE),
#'        Use *_DISABLE_DEPRECATED: @0@'.format(enable_deprecation_flags),
'',
'        GConf support:            @0@'.format(gconf.found()),
'        Logind support:           @0@'.format(logind.found()),
'        IPv6 support:             @0@'.format(have_ipv6),
'        Backtrace support:        @0@'.format(backtrace.found()),
'        XRender support:          @0@'.format(xrender.found()),
'        XTest support:            @0@'.format(xtest.found()),
'        Build documentation:      @0@'.format(docbook_enabled),
'',
]))
