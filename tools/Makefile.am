bin_PROGRAMS = cinnamon-session-quit
libexec_PROGRAMS = cinnamon-session-check-accelerated cinnamon-session-check-accelerated-helper

AM_CPPFLAGS =

AM_CFLAGS = $(WARN_CFLAGS)

cinnamon_session_quit_SOURCES =			\
	cinnamon-session-quit.c

cinnamon_session_quit_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(CINNAMON_SESSION_CFLAGS)			\
	$(DBUS_GLIB_CFLAGS)			\
	$(GCONF_FLAGS)				\
	-DLOCALE_DIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED_CFLAGS)

cinnamon_session_quit_LDADD =			\
	$(SM_LIBS)				\
	$(ICE_LIBS)				\
	$(CINNAMON_SESSION_LIBS)			\
	$(DBUS_GLIB_LIBS)			\
	$(GCONF_LIBS)

cinnamon_session_check_accelerated_helper_SOURCES = 		\
	cinnamon-session-check-accelerated-helper.c

cinnamon_session_check_accelerated_helper_CPPFLAGS =	\
	-DPKGDATADIR=\""$(pkgdatadir)"\"		\
	$(GL_TEST_CFLAGS)

cinnamon_session_check_accelerated_helper_LDADD = 		\
	$(GL_TEST_LIBS)				\
	$(X_LIBS)

cinnamon_session_check_accelerated_SOURCES =       \
	cinnamon-session-check-accelerated.c

cinnamon_session_check_accelerated_CPPFLAGS =      \
	-DLIBEXECDIR=\""$(libexecdir)"\"	\
	$(AM_CPPFLAGS)			        \
	$(GTK3_CFLAGS)

cinnamon_session_check_accelerated_LDADD =         \
	$(GTK3_LIBS)				\
	$(X_LIBS)

-include $(top_srcdir)/git.mk
