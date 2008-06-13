XSLTPROC = /usr/bin/xsltproc
XMLLINT = /usr/bin/xmllint

dot_html_in = $(wildcard *.html.in)
dot_html = $(dot_html_in:%.html.in=%.html)

EXTRA_DIST = site.xsl $(dot_html) $(dot_html_in)

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(HTML_DIR)
	@INSTALL@ -m 0644 $(srcdir)/*.html $(DESTDIR)$(HTML_DIR)

all: $(dot_html)

%.html.tmp:	%.html.in site.xsl page.xsl sitemap.html.in
		@(if [ -x $(XSLTPROC) ] ; then \
		echo "Generating $@"; \
		name=`echo $@ | sed -e 's/.tmp//'`; \
		echo "name: $$name"; \
		$(XSLTPROC) --stringparam pagename $$name --nonet --html $(top_srcdir)/doc/site.xsl $< > $@ || (rm $@ && exit 1) ; fi )


%.html:	%.html.tmp
	@(if [ -x $(XMLLINT) ] ; then \
	echo "Validating $@" ; \
	$(XMLLINT) --nonet --format --valid $< > $@ || : ; fi );
