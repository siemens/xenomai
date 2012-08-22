HTML_DOCS=README.INSTALL TROUBLESHOOTING \
	asciidoc-icons asciidoc-icons/callout asciidoc-css asciidoc-js
PDF_DOCS=README.INSTALL.pdf TROUBLESHOOTING.pdf
EXTRA_DIST=README.INSTALL.adoc TROUBLESHOOTING.adoc plaintext.conf plaintext_postproc.awk plaintext.xsl

if !CONFIG_XENO_ASCIIDOC
HTML_DOCSDIR = $(srcdir)/../generated/html/
PDF_DOCSDIR = $(srcdir)/../generated/pdf/
else
HTML_DOCSDIR = ./
PDF_DOCSDIR = ./

TXT_DOCS=README.INSTALL.txt TROUBLESHOOTING.txt

ASCIIDOC_HTML_OPTS=-a icons -a iconsdir=../asciidoc-icons \
	-a linkcss -a stylesdir=../asciidoc-css -a scriptsdir=../asciidoc-js \
	-a toc -a toclevels=3 -a max-width=55em -a xenover=$(PACKAGE_VERSION)

ASCIIDOC_PDF_OPTS=-a icons -a toc -a toclevels=3 -a xenover=$(PACKAGE_VERSION)

ASCIIDOC_TXT_OPTS=-a xenover=$(PACKAGE_VERSION) -a encoding=ascii

tmpdir=adoc_plaintext

all-local: $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS)

%: %.adoc Makefile
	@$(mkdir_p) $@
	$(ASCIIDOC) -n -b xhtml11 $(ASCIIDOC_HTML_OPTS) -o $@/index.html $<

%.pdf: %.adoc Makefile
	$(A2X) -f pdf -D . $(ASCIIDOC_PDF_OPTS) $<

$(tmpdir)/%.txt: %.adoc Makefile plaintext.conf plaintext.xsl
	@$(mkdir_p) $(tmpdir)
	$(ASCIIDOC) --backend docbook -f $(srcdir)/plaintext.conf \
		--doctype article $(ASCIIDOC_TXT_OPTS) \
		--out-file $(tmpdir)/$*.xml $<
	xsltproc --stringparam toc.section.depth 3 --nonet \
		--output $(tmpdir)/$*.html $(srcdir)/plaintext.xsl \
		$(tmpdir)/$*.xml
	w3m -cols 80 -dump -T text/html -no-graph $(tmpdir)/$*.html > $@

%.txt: $(tmpdir)/%.txt Makefile plaintext_postproc.awk
	awk -f $(srcdir)/plaintext_postproc.awk $(tmpdir)/$*.txt > $@

asciidoc-icons asciidoc-icons/callout:
	@cp -a /usr/share/asciidoc/icons $@

asciidoc-css:
	@$(mkdir_p) $@
	@cp -a /usr/share/asciidoc/stylesheets/xhtml11.css $@
	@cp -a /usr/share/asciidoc/stylesheets/xhtml11-quirks.css $@

asciidoc-js:
	@$(mkdir_p) $@
	@cp -a /usr/share/asciidoc/javascripts/asciidoc-xhtml11.js $@
	@cp -a /usr/share/asciidoc/javascripts/toc.js $@

endif

include $(top_srcdir)/doc/install-dist.rules

install-data-local: install-docs-local

dist-hook: dist-docs-hook

uninstall-local: uninstall-docs

clean-local:
	$(RM) -R $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(tmpdir)

distclean-local: clean-local
