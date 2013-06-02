SUBDIRS = icons

uidir = $(pkgdatadir)
ui_DATA = \
	session-properties.ui	\
	gsm-inhibit-dialog.ui

hwcompatdir = $(pkgdatadir)
hwcompat_DATA = hardware-compatibility

desktopdir = $(datadir)/applications
desktop_in_files = session-properties.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_XML_NOMERGE_RULE@

gsettings_SCHEMAS = org.gnome.SessionManager.gschema.xml
@GSETTINGS_RULES@

migrationdir = $(datadir)/GConf/gsettings
dist_migration_DATA = gnome-session.convert

EXTRA_DIST =					\
	$(gsettings_SCHEMAS:.xml=.xml.in)	\
	$(ui_DATA)				\
	$(hwcompat_DATA)

CLEANFILES =					\
	$(gsettings_SCHEMAS)			\
	$(desktop_DATA)


DISTCLEANFILES =				\
	$(gsettings_SCHEMAS)			\
	$(desktop_in_files)

-include $(top_srcdir)/git.mk
