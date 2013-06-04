bin_PROGRAMS = cinnamon-session
noinst_LTLIBRARIES = libcsmutil.la
noinst_PROGRAMS = 		\
	test-client-dbus	\
	test-inhibit		\
	test-process-helper

AM_CPPFLAGS =					\
	$(CINNAMON_SESSION_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

cinnamon_session_SOURCES =				\
	csm-app.h				\
	csm-app.c				\
	csm-autostart-app.h			\
	csm-autostart-app.c			\
	csm-client.c				\
	csm-client.h				\
	csm-xsmp-client.h			\
	csm-xsmp-client.c			\
	csm-dbus-client.h			\
	csm-dbus-client.c			\
	csm-fail-whale-dialog.h			\
	csm-fail-whale-dialog.c			\
	csm-marshal.h				\
	csm-marshal.c				\
	csm-system.h				\
	csm-system.c				\
	csm-consolekit.c			\
	csm-consolekit.h			\
	csm-systemd.h				\
	csm-systemd.c				\
	csm-logout-dialog.h			\
	csm-logout-dialog.c			\
	csm-icon-names.h			\
	csm-inhibit-dialog.h			\
	csm-inhibit-dialog.c			\
	cs-idle-monitor.h			\
	cs-idle-monitor.c			\
	csm-presence.h				\
	csm-presence.c				\
	mdm.h					\
	mdm.c					\
	mdm-signal-handler.h			\
	mdm-signal-handler.c			\
	mdm-log.h				\
	mdm-log.c				\
	main.c					\
	csm-store.h				\
	csm-store.c				\
	csm-inhibitor.h				\
	csm-inhibitor.c				\
	csm-manager.c				\
	csm-manager.h				\
	csm-process-helper.c			\
	csm-process-helper.h			\
	csm-session-fill.c			\
	csm-session-fill.h			\
	csm-session-save.c			\
	csm-session-save.h			\
	csm-xsmp-server.c			\
	csm-xsmp-server.h

cinnamon_session_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(SM_CFLAGS)				\
	$(ICE_CFLAGS)				\
	$(XEXT_CFLAGS)				\
	$(GCONF_CFLAGS)				\
	$(SYSTEMD_CFLAGS)			\
	-I$(top_srcdir)/egg			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"	\
	-DDATA_DIR=\""$(datadir)/cinnamon-session"\" \
	-DLIBEXECDIR=\"$(libexecdir)\"		\
	-DGTKBUILDER_DIR=\""$(pkgdatadir)"\"	\
	-DGCONF_SANITY_CHECK=\""$(GCONF_SANITY_CHECK)"\" \
	-DGCONFTOOL_CMD=\"$(GCONFTOOL)\"

cinnamon_session_LDADD =				\
	libcsmutil.la 				\
	$(top_builddir)/egg/libeggdesktopfile.la \
	$(SM_LIBS)				\
	$(ICE_LIBS)				\
	$(XRENDER_LIBS)				\
	$(XTEST_LIBS)				\
	$(XEXT_LIBS)				\
	$(CINNAMON_SESSION_LIBS)			\
	$(GCONF_LIBS)				\
	$(SYSTEMD_LIBS)				\
	$(EXECINFO_LIBS)

libcsmutil_la_SOURCES =				\
	csm-util.c				\
	csm-util.h

libcsmutil_la_LIBADD = 				\
	$(CINNAMON_SESSION_LIBS)

test_inhibit_SOURCES = test-inhibit.c
test_inhibit_LDADD = $(CINNAMON_SESSION_LIBS)

test_client_dbus_SOURCES = test-client-dbus.c
test_client_dbus_LDADD = $(DBUS_GLIB_LIBS)

test_process_helper_SOURCES = test-process-helper.c csm-process-helper.c csm-process-helper.h
test_process_helper_LDADD = $(DBUS_GLIB_LIBS)

csm-marshal.c: csm-marshal.list
	$(AM_V_GEN)echo "#include \"csm-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --prefix=csm_marshal --body >> $@

csm-marshal.h: csm-marshal.list
	$(AM_V_GEN)$(GLIB_GENMARSHAL) $< --prefix=csm_marshal --header > $@

csm-manager-glue.h: org.gnome.SessionManager.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=csm_manager --mode=glib-server --output=csm-manager-glue.h $(srcdir)/org.gnome.SessionManager.xml

csm-client-glue.h: org.gnome.SessionManager.Client.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=csm_client --mode=glib-server --output=csm-client-glue.h $(srcdir)/org.gnome.SessionManager.Client.xml

csm-app-glue.h: org.gnome.SessionManager.App.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=csm_app --mode=glib-server --output=csm-app-glue.h $(srcdir)/org.gnome.SessionManager.App.xml

csm-inhibitor-glue.h: org.gnome.SessionManager.Inhibitor.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=csm_inhibitor --mode=glib-server --output=csm-inhibitor-glue.h $(srcdir)/org.gnome.SessionManager.Inhibitor.xml

csm-presence-glue.h: org.gnome.SessionManager.Presence.xml Makefile.am
	$(AM_V_GEN)dbus-binding-tool --prefix=csm_presence --mode=glib-server --output=csm-presence-glue.h $(srcdir)/org.gnome.SessionManager.Presence.xml

BUILT_SOURCES =			\
	csm-marshal.c		\
	csm-marshal.h		\
	csm-manager-glue.h	\
	csm-presence-glue.h	\
	csm-inhibitor-glue.h	\
	csm-client-glue.h	\
	csm-app-glue.h

EXTRA_DIST =						\
	README						\
	csm-marshal.list				\
	org.gnome.SessionManager.xml			\
	org.gnome.SessionManager.App.xml		\
	org.gnome.SessionManager.Client.xml		\
	org.gnome.SessionManager.ClientPrivate.xml	\
	org.gnome.SessionManager.Inhibitor.xml		\
	org.gnome.SessionManager.Presence.xml

CLEANFILES =	\
	$(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
