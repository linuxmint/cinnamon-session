bin_PROGRAMS = cinnamon-session-properties

AM_CPPFLAGS =						\
	$(SESSION_PROPERTIES_CFLAGS)			\
	$(GCONF_CFLAGS)					\
	-I$(top_srcdir)/cinnamon-session			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"		\
	-DGTKBUILDER_DIR=\""$(pkgdatadir)"\"		\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

cinnamon_session_properties_SOURCES =			\
	main.c						\
	csm-properties-dialog.h				\
	csm-properties-dialog.c				\
	csm-app-dialog.h				\
	csm-app-dialog.c				\
	csp-app.h					\
	csp-app.c					\
	csp-app-manager.h				\
	csp-app-manager.c				\
	csp-keyfile.h					\
	csp-keyfile.c

cinnamon_session_properties_LDADD =			\
	$(SESSION_PROPERTIES_LIBS)			\
	$(top_builddir)/cinnamon-session/libcsmutil.la 	\
	$(GCONF_LIBS)

-include $(top_srcdir)/git.mk
