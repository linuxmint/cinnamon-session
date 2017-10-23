AC_INIT([cinnamon-session],
        [3.6.0],
        [https://github.com/linuxmint/cinnamon-session/issues])

AC_CONFIG_SRCDIR([cinnamon-session])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

m4_ifdef([AX_IS_RELEASE], [AX_IS_RELEASE([always])])

AM_INIT_AUTOMAKE([1.11 no-dist-gzip dist-xz tar-ustar])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Check for programs
AC_PROG_CC
AM_PROG_CC_C_O
AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
PKG_PROG_PKG_CONFIG()

# Initialize libtool
LT_PREREQ([2.2.6])
LT_INIT([dlopen disable-static])

m4_ifdef([AX_COMPILER_FLAGS],
         [AX_COMPILER_FLAGS([WARN_CFLAGS],[WARN_LDFLAGS])])

AC_ARG_ENABLE(deprecation_flags,
              [AS_HELP_STRING([--enable-deprecation-flags],
                              [use *_DISABLE_DEPRECATED flags @<:@default=no@:>@])],,
              [enable_deprecation_flags=no])

if test "x$enable_deprecation_flags" = "xyes"; then
   DISABLE_DEPRECATED_CFLAGS=$DISABLE_DEPRECATED
   AC_SUBST([DISABLE_DEPRECATED_CFLAGS])
fi

GLIB_REQUIRED=2.37.3
GTK3_REQUIRED=3.0.0
DBUS_GLIB_REQUIRED=0.88
UPOWER_REQUIRED=0.9.0

dnl ====================================================================
dnl Dependency Checks
dnl ====================================================================

dnl Standard vertical stacks
PKG_CHECK_MODULES(GIO, gio-2.0)
PKG_CHECK_MODULES(GTK3, gtk+-3.0 >= $GTK3_REQUIRED)

PKG_CHECK_MODULES(CINNAMON_SESSION,
        glib-2.0 >= $GLIB_REQUIRED
        gio-2.0 >= $GLIB_REQUIRED
        gtk+-3.0 >= $GTK3_REQUIRED
        dbus-glib-1 >= $DBUS_GLIB_REQUIRED
        libcanberra
)

dnl We can only support old upower
dnl https://bugzilla.gnome.org/show_bug.cgi?id=710383
PKG_CHECK_MODULES(UPOWER, upower-glib < 0.99.0, have_old_upower=yes, have_old_upower=no)
AS_IF([test x$have_old_upower = xyes], [
  AC_DEFINE([HAVE_OLD_UPOWER], [1], [Define if we have an older upower])
])
AM_CONDITIONAL(HAVE_OLD_UPOWER, test x$have_old_upower = xyes)

PKG_CHECK_MODULES(SM, sm)
PKG_CHECK_MODULES(ICE, ice)
PKG_CHECK_MODULES(XEXT, xext xau)

PKG_CHECK_MODULES(DBUS_GLIB, dbus-glib-1 >= $DBUS_GLIB_REQUIRED)

PKG_CHECK_MODULES(EGG_SMCLIENT, gtk+-3.0)

PKG_CHECK_MODULES(GL_TEST, xcomposite gl)

dnl ====================================================================
dnl Check for gconf
dnl ====================================================================
AC_ARG_ENABLE([gconf],
              AS_HELP_STRING([--enable-gconf], [Support gconf-based autostart]),
              [enable_gconf=$enableval],
              [enable_gconf=auto])

PKG_CHECK_MODULES(GCONF, gconf-2.0, [have_gconf=yes], [have_gconf=no])

if test x$enable_gconf = xauto ; then
        enable_gconf=$have_gconf
elif test x$enable_gconf = xyes -a x$have_gconf = xno ; then
        AC_MSG_ERROR([GConf support explicitly required, but gconf not found])
fi

if test x$enable_gconf = xyes ; then
        AC_DEFINE([HAVE_GCONF], [1], [Define if we support gconf-based autostart])
fi

dnl ====================================================================
dnl Check for logind
dnl ====================================================================

PKG_CHECK_MODULES(LOGIND, [gio-unix-2.0 libsystemd-login], [have_logind=yes], [
 PKG_CHECK_MODULES(LOGIND, [gio-unix-2.0 libsystemd], [have_logind=yes], [have_logind=no])
])

if test x$have_logind = xyes; then
    AC_DEFINE(HAVE_LOGIND, 1, [Define if logind is supported])
fi

AC_SUBST(LOGIND_CFLAGS)
AC_SUBST(LOGIND_LIBS)

dnl ====================================================================
dnl Check for qt 5.7+ to set correct env var for theme/styling
dnl ====================================================================
AC_ARG_ENABLE(qt57_theme_support,
              AS_HELP_STRING([--enable-qt57-theme-support], [Support GTK styles for QT apps with QT 5.7+]),
              [enable_qt57_theme_support=yes],
              [enable_qt57_theme_support=no])

if test x$enable_qt57_theme_support = xyes; then
    AC_DEFINE([HAVE_QT57], [1], [Have QT 5.7+])
else
    AC_DEFINE([HAVE_QT57], [0], [Have QT 5.7+])
fi

dnl ====================================================================
dnl X development libraries check
dnl ====================================================================

# If Pango included the shared library dependencies from X11 in
# the pkg-config output, then we use that (to avoid duplicates).
# but if they were omitted to avoid binary compatibility problems
# then we need to repeat the checks.

if $PKG_CONFIG --exists pangoxft ; then
  PANGO_PACKAGES="pangox pangoxft"
else
  PANGO_PACKAGES="pangox"
fi

x_libs="`$PKG_CONFIG --libs $PANGO_PACKAGES`"
case x_libs in
  *-lX11*) pango_omitted_x_deps=no ;;
  *)       pango_omitted_x_deps=yes ;;
esac

if test $pango_omitted_x_deps = yes ; then
  AC_PATH_XTRA

  if test x$no_x = xyes ; then
    AC_MSG_ERROR([X development libraries not found])
  else
    X_LIBS="$X_PRE_LIBS $X_LIBS -lX11 $X_EXTRA_LIBS"
  fi
fi

AC_CHECK_LIB(Xau, XauFileName, [X_LIBS="$X_LIBS -lXau"],
             [AC_MSG_ERROR([
*** Can't find the Xauth library. It is needed to compile cinnamon-session.])],
             $X_LIBS)

AC_SUBST(X_LIBS)

dnl ====================================================================
dnl Check for XSync extension
dnl ====================================================================

have_xsync=no
AC_CHECK_HEADER(X11/extensions/sync.h, [have_xsync=yes],,
                    [#include <X11/Xlib.h>])
if test "$have_xsync" = yes; then
  AC_DEFINE(HAVE_XSYNC, 1, [Have the SYNC extension library])
fi

dnl ====================================================================
dnl Check for XTest extension
dnl ====================================================================

have_xtest=no
PKG_CHECK_MODULES(XTEST, xtst, have_xtest=yes, have_xtest=no)
if test "$have_xtest" = yes; then
    AC_DEFINE(HAVE_XTEST, 1, [Have the XTest extension library])
fi
AC_SUBST(HAVE_XTEST)
AC_SUBST(XTEST_CFLAGS)
AC_SUBST(XTEST_LIBS)

dnl ====================================================================
dnl XRender checks
dnl ====================================================================

PKG_CHECK_MODULES(XRENDER, xrender, have_xrender=yes, have_xrender=no)
AM_CONDITIONAL(HAVE_XRENDER, test x$have_xrender = xyes)
if test $have_xrender=yes; then
    AC_DEFINE(HAVE_XRENDER, 1, [Have the Render X extension])
fi
AC_SUBST(HAVE_XRENDER)
AC_SUBST(XRENDER_CFLAGS)
AC_SUBST(XRENDER_LIBS)

dnl ====================================================================
dnl - DocBook Documentation
dnl ====================================================================

AC_ARG_ENABLE(docbook-docs,
              [AS_HELP_STRING([--enable-docbook-docs],
                              [build documentation (requires xmlto)])],
              [enable_docbook_docs=$enableval],
              [enable_docbook_docs=auto])
AC_PATH_PROG(XMLTO, xmlto, no)
AC_MSG_CHECKING([whether to build DocBook documentation])
if test x$XMLTO = xno ; then
    have_docbook=no
else
    have_docbook=yes
fi
if test x$enable_docbook_docs = xauto ; then
    if test x$have_docbook = xno ; then
        enable_docbook_docs=no
    else
        enable_docbook_docs=yes
    fi
fi
if test x$enable_docbook_docs = xyes; then
    if test x$have_docbook = xno; then
        AC_MSG_ERROR([Building DocBook docs explicitly required, but DocBook not found])
    fi
fi
AM_CONDITIONAL(DOCBOOK_DOCS_ENABLED, test x$enable_docbook_docs = xyes)
AC_MSG_RESULT($enable_docbook_docs)

dnl ====================================================================
dnl Check for xsltproc
dnl ====================================================================
AC_PATH_PROG([XSLTPROC], [xsltproc])

dnl ====================================================================
dnl Language Support
dnl ====================================================================
IT_PROG_INTLTOOL([0.40.6])

GETTEXT_PACKAGE=cinnamon-session
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE",
                   [The gettext translation domain])
AC_SUBST(GETTEXT_PACKAGE)

AM_GLIB_GNU_GETTEXT
GLIB_GSETTINGS

dnl ====================================================================
dnl Headers
dnl ====================================================================
AC_HEADER_STDC
AC_CHECK_HEADERS(syslog.h tcpd.h sys/param.h)

dnl ====================================================================
dnl check for backtrace support
dnl ====================================================================

AC_CHECK_HEADERS(execinfo.h)
AC_CHECK_LIB(execinfo, backtrace, [have_backtrace="yes"], [have_backtrace="no"])
EXECINFO_LIBS=""
if test "x$have_backtrace" = "xyes"; then
        EXECINFO_LIBS="-lexecinfo"
fi
AC_SUBST(EXECINFO_LIBS)

dnl ====================================================================
dnl Check for X transport interface - allows to disable ICE Transports
dnl See also https://bugzilla.gnome.org/show_bug.cgi?id=725100
dnl ====================================================================
AC_ARG_WITH([xtrans],
	    [AS_HELP_STRING([--without-xtrans],
			   [Build without xtrans support (results in ICE listening on remote TCP ports)])],
	    [],
	    [with_xtrans=yes])
AS_IF([test "$with_xtrans" != "no"],
      [PKG_CHECK_MODULES(XTRANS, xtrans,
      [AC_DEFINE(HAVE_XTRANS, 1, [Have the X Transport library])])])
AC_SUBST(HAVE_XTRANS)

dnl ====================================================================
dnl Code for checking whether IPv6 is enabled on the system....
dnl ====================================================================
AC_MSG_CHECKING([whether to enable ipv6])
AC_ARG_ENABLE(ipv6,
              [AS_HELP_STRING([--enable-ipv6],
                              [enable IPv6 extensions])],,
              [enable_ipv6=yes])
have_full_ipv6=no
if test $enable_ipv6 = yes; then

  dnl ====================================================================
  dnl Code for checking presence of AF_INET6 on the system....
  dnl ====================================================================
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <sys/types.h>
    #include <sys/socket.h>]], [[
    socket(AF_INET6, SOCK_STREAM, 0)
    ]])],
    [have_ipv6=yes],
    [have_ipv6=no]
  )
  AC_MSG_RESULT($have_ipv6)

  dnl =================================================================
  dnl Now we would check for specific function like getaddrinfo.
  dnl =================================================================
  have_getaddrinfo=no
  if test $have_ipv6=yes; then
    AC_CHECK_FUNC(getaddrinfo, have_getaddrinfo=yes)
    if test $have_getaddrinfo != yes; then
      # getaddrinfo is not in the default libraries.  See if it's in some other.
      for lib in bsd socket inet; do
        AC_CHECK_LIB($lib, getaddrinfo, [LIBS="$LIBS -l$lib";have_getaddrinfo=yes; break])
      done
    fi
    if test $have_getaddrinfo=yes; then
      AC_DEFINE(ENABLE_IPV6, 1, [Define if IPV6 is supported])
      have_full_ipv6=yes
    fi
  fi
fi
dnl ==============================================================================
dnl End of IPv6 checks
dnl ==============================================================================

AC_CONFIG_FILES([
Makefile
doc/Makefile
doc/dbus/Makefile
doc/dbus/cinnamon-session.xml
doc/man/Makefile
data/Makefile
data/org.cinnamon.SessionManager.gschema.xml
data/icons/Makefile
data/icons/16x16/Makefile
data/icons/22x22/Makefile
data/icons/24x24/Makefile
data/icons/32x32/Makefile
data/icons/48x48/Makefile
data/icons/scalable/Makefile
egg/Makefile
cinnamon-session/Makefile
tools/Makefile
po/Makefile.in
])
AC_OUTPUT

dnl ---------------------------------------------------------------------------
dnl - Show summary
dnl ---------------------------------------------------------------------------

echo "
              cinnamon-session $VERSION
              `echo cinnamon-session $VERSION | sed "s/./=/g"`

        prefix:                   ${prefix}
        exec_prefix:              ${exec_prefix}
        libdir:                   ${libdir}
        bindir:                   ${bindir}
        sbindir:                  ${sbindir}
        sysconfdir:               ${sysconfdir}
        localstatedir:            ${localstatedir}
        datadir:                  ${datadir}
        source code location:     ${srcdir}
        compiler:                 ${CC}
        cflags:                   ${CFLAGS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}
        Use *_DISABLE_DEPRECATED: ${enable_deprecation_flags}

        GConf support:            ${enable_gconf}
        Logind support:           ${have_logind}
        Qt 5.7+ theme support:    ${enable_qt57_theme_support}
        IPv6 support:             ${have_full_ipv6}
        Backtrace support:        ${have_backtrace}
        XRender support:          ${have_xrender}
        XSync support:            ${have_xsync}
        XTest support:            ${have_xtest}
        Legacy UPower backend:    ${have_old_upower}
        Build documentation:      ${enable_docbook_docs}

"
