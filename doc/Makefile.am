XSLTPROC = /usr/bin/xsltproc

WEB_PAGES = index.html \
            architecture.html \
            downloads.html \
            intro.html \
            news.html \
            platforms.html \
            schema.html

EXTRA_DIST = site.xsl libvirt-cim.html $(WEB_PAGES)

$(WEB_PAGES): libvirt-cim.html site.xsl
	-@(if [ -x $(XSLTPROC) ]; then \
	   $(XSLTPROC) --nonet --html $(top_srcdir)/doc/site.xsl $(top_srcdir)/doc/libvirt-cim.html > index.html; \
	   fi);

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(HTML_DIR)
	@INSTALL@ -m 0644 $(srcdir)/*.html $(DESTDIR)$(HTML_DIR)

all: $(WEB_PAGES)
