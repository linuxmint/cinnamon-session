SUBDIRS = icons

uidir = $(pkgdatadir)
ui_DATA = \
	csm-inhibit-dialog.glade

hwcompatdir = $(pkgdatadir)
hwcompat_DATA = hardware-compatibility

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
	$(gsettings_SCHEMAS)

-include $(top_srcdir)/git.mk
