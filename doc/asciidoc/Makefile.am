HTML_DOCS = \
	README.INSTALL \
	TROUBLESHOOTING \
	asciidoc-icons \
	asciidoc-icons/callouts \
	asciidoc-css \
	asciidoc-js

PDF_DOCS = \
	README.INSTALL.pdf \
	TROUBLESHOOTING.pdf

EXTRA_DIST = \
	README.INSTALL.adoc \
	TROUBLESHOOTING.adoc \
	plaintext.conf \
	plaintext_postproc.awk \
	plaintext.xsl \
	asciidoc.rules

if !CONFIG_XENO_ASCIIDOC
HTML_DOCSDIR = $(srcdir)/../generated/html/
PDF_DOCSDIR = $(srcdir)/../generated/pdf/
else
HTML_DOCSDIR = ./
PDF_DOCSDIR = ./

TXT_DOCS = \
	README.INSTALL.txt \
	TROUBLESHOOTING.txt

all-local: $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS)

asciidoc-icons asciidoc-icons/callouts: FORCE
	@$(RM) -R asciidoc-icons
	@cp -a /usr/share/asciidoc/icons asciidoc-icons

asciidoc-css: FORCE
	@$(mkdir_p) $@
	@cp -a /usr/share/asciidoc/stylesheets/asciidoc.css $@
	@cp -a /usr/share/asciidoc/stylesheets/xhtml11.css $@
	@cp -a /usr/share/asciidoc/stylesheets/xhtml11-quirks.css $@

asciidoc-js: FORCE
	@$(mkdir_p) $@
	@cp -a /usr/share/asciidoc/javascripts/asciidoc.js $@
	@cp -a /usr/share/asciidoc/javascripts/toc.js $@

.PHONY: FORCE

endif

include $(top_srcdir)/doc/asciidoc/asciidoc.rules

install-data-local: install-docs-local

dist-hook: dist-docs-hook

uninstall-local: uninstall-docs

clean-local:
	$(RM) -R $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(tmpdir)

distclean-local: clean-local
