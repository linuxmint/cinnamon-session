SPEC_XML_FILES =					\
	cinnamon-session.xml				\
	org.cinnamon.SessionManager.ref.xml		\
	org.cinnamon.SessionManager.Client.ref.xml		\
	org.cinnamon.SessionManager.ClientPrivate.ref.xml	\
	org.cinnamon.SessionManager.Inhibitor.ref.xml	\
	org.cinnamon.SessionManager.Presence.ref.xml

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(docdir)/dbus
htmldoc_DATA = cinnamon-session.html

cinnamon-session.html: $(SPEC_XML_FILES)
	$(AM_V_GEN)$(XMLTO) xhtml-nochunks -m $(top_srcdir)/doc/dbus/config.xsl cinnamon-session.xml

endif # DOCBOOK_DOCS_ENABLED

org.cinnamon.SessionManager.ref.xml: $(top_srcdir)/cinnamon-session/org.cinnamon.SessionManager.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.cinnamon.SessionManager.Client.ref.xml: $(top_srcdir)/cinnamon-session/org.cinnamon.SessionManager.Client.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.cinnamon.SessionManager.ClientPrivate.ref.xml: $(top_srcdir)/cinnamon-session/org.cinnamon.SessionManager.ClientPrivate.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.cinnamon.SessionManager.Inhibitor.ref.xml: $(top_srcdir)/cinnamon-session/org.cinnamon.SessionManager.Inhibitor.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.cinnamon.SessionManager.Presence.ref.xml: $(top_srcdir)/cinnamon-session/org.cinnamon.SessionManager.Presence.xml spec-to-docbook.xsl
	$(AM_V_GEN)$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@

BUILT_SOURCES =	\
	org.cinnamon.SessionManager.ref.xml \
	org.cinnamon.SessionManager.Client.ref.xml \
	org.cinnamon.SessionManager.ClientPrivate.ref.xml \
	org.cinnamon.SessionManager.Inhibitor.ref.xml \
	org.cinnamon.SessionManager.Presence.ref.xml

CLEANFILES =				\
	$(BUILT_SOURCES)		\
	$(htmldoc_DATA)

EXTRA_DIST =				\
	cinnamon-session.xml.in		\
	config.xsl			\
	docbook.css			\
	dbus-introspect-docs.dtd	\
	spec-to-docbook.xsl

-include $(top_srcdir)/git.mk
