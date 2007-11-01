XSLTPROC = /usr/bin/xsltproc

WEB_PAGES = index.html

$(WEB_PAGES): libvirt-cim.html site.xsl
	$(XSLTPROC) --nonet --html $(top_srcdir)/doc/site.xsl $(top_srcdir)/doc/libvirt-cim.html > index.html

all: $(WEB_PAGES)

