SUBDIRS = icons

uidir = $(pkgdatadir)
ui_DATA = \
	cinnamon-session-properties.glade	\
	csm-inhibit-dialog.glade

hwcompatdir = $(pkgdatadir)
hwcompat_DATA = hardware-compatibility

desktopdir = $(datadir)/applications
desktop_in_files = cinnamon-session-properties.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_XML_NOMERGE_RULE@

gsettings_SCHEMAS = org.cinnamon.SessionManager.gschema.xml
@GSETTINGS_RULES@

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
