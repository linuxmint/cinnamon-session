SUBDIRS = icons

uidir = $(pkgdatadir)
ui_DATA = \
	session-properties.ui	\
	gsm-inhibit-dialog.ui

hwcompatdir = $(pkgdatadir)
hwcompat_DATA = hardware-compatibility

xsessiondir = $(datadir)/xsessions
xsession_in_files = gnome.desktop.in
xsession_DATA = $(xsession_in_files:.desktop.in=.desktop)

desktopdir = $(datadir)/applications
desktop_in_files = session-properties.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

sessiondir = $(datadir)/gnome-session/sessions
session_in_in_files = gnome.session.desktop.in.in gnome-fallback.session.desktop.in.in
session_in_files = $(session_in_in_files:.session.desktop.in.in=.session.desktop.in)
session_DATA = $(session_in_files:.session.desktop.in=.session)

%.session.desktop.in: %.session.desktop.in.in Makefile
	$(AM_V_GEN)sed \
		-e "s|\@LIBEXECDIR\@|$(libexecdir)|" \
		$< > $@

%.session: %.session.desktop.in   $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*.po) ; LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@

@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_XML_NOMERGE_RULE@

gsettings_SCHEMAS = org.gnome.SessionManager.gschema.xml
@GSETTINGS_RULES@

migrationdir = $(datadir)/GConf/gsettings
dist_migration_DATA = gnome-session.convert

EXTRA_DIST =					\
	$(xsession_in_files)			\
	$(session_in_in_files)			\
	$(gsettings_SCHEMAS:.xml=.xml.in)	\
	$(ui_DATA)				\
	$(hwcompat_DATA)

CLEANFILES =					\
	$(gsettings_SCHEMAS)			\
	$(xsession_DATA)			\
	$(desktop_DATA)				\
	$(session_DATA)

DISTCLEANFILES =				\
	$(gsettings_SCHEMAS)			\
	$(desktop_in_files)

-include $(top_srcdir)/git.mk
