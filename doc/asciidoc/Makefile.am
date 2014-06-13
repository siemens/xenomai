HTML_DOCS = 			\
	README.INSTALL		\
	README.APPLICATIONS 	\
	TROUBLESHOOTING 	\
	MIGRATION 		\
	asciidoc-icons		\
	asciidoc-icons/callouts	\
	asciidoc-css		\
	asciidoc-js

PDF_DOCS = 			\
	README.INSTALL.pdf 	\
	README.APPLICATIONS.pdf \
	TROUBLESHOOTING.pdf	\
	MIGRATION.pdf

TXT_DOCS =			\
	README.INSTALL.txt	\
	README.APPLICATIONS.txt \
	TROUBLESHOOTING.txt	\
	MIGRATION.txt

MAN_DOCS = 		\
	clocktest.1 	\
	cyclictest.1 	\
	latency.1 	\
	rtcanconfig.1 	\
	rtcanrecv.1 	\
	rtcansend.1 	\
	switchtest.1 	\
	xeno.1 		\
	xeno-config.1 	\
	xeno-test.1 	\
	dohell.1

EXTRA_DIST :=				\
	README.INSTALL.adoc 		\
	README.APPLICATIONS.adoc	\
	TROUBLESHOOTING.adoc 		\
	MIGRATION.adoc 			\
	plaintext.conf 			\
	plaintext_postproc.awk		\
	plaintext.xsl			\
	$(MAN_DOCS:%.1:%.adoc)

if !XENO_BUILD_DOC
HTML_DOCSDIR = $(srcdir)/../prebuilt/html/
PDF_DOCSDIR = $(srcdir)/../prebuilt/pdf/
MAN_DOCSDIR = $(srcdir)/../prebuilt/man/
else
HTML_DOCSDIR = ./
PDF_DOCSDIR = ./
MAN_DOCSDIR = ./

ASCIIDOC_HTML_OPTS=-a icons -a iconsdir=../asciidoc-icons \
	-a linkcss -a stylesdir=../asciidoc-css -a scriptsdir=../asciidoc-js \
	-a toc -a toclevels=3 -a max-width=55em -a xenover=$(PACKAGE_VERSION)

ASCIIDOC_PDF_OPTS=-a icons -a toc -a toclevels=3 -a xenover=$(PACKAGE_VERSION)

ASCIIDOC_MAN_OPTS=-a xenover=$(PACKAGE_VERSION)

ASCIIDOC_TXT_OPTS=-a xenover=$(PACKAGE_VERSION) -a encoding=ascii

tmpdir=adoc_plaintext

all-local: $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(MAN_DOCS)

%: %.adoc Makefile
	@$(mkdir_p) $@
	$(ASCIIDOC) -n -b xhtml11 $(ASCIIDOC_HTML_OPTS) -o $@/index.html $<

%.1: %.adoc Makefile
	$(A2X) -f manpage -D . $(ASCIIDOC_MAN_OPTS) $<       

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

asciidoc-icons asciidoc-icons/callouts:
	@$(RM) -R asciidoc-icons
	@test -d /usr/share/asciidoc/images && \
	cp -a /usr/share/asciidoc/images/icons asciidoc-icons || \
	cp -a /usr/share/asciidoc/icons asciidoc-icons

asciidoc-css:
	@$(mkdir_p) $@
	@test -d /usr/share/asciidoc/stylesheets/xhtml11.css && \
	@cp -a /usr/share/asciidoc/stylesheets/xhtml11.css $@ || \
	true
	@cp -a /usr/share/asciidoc/stylesheets/xhtml11-quirks.css $@

asciidoc-js:
	@$(mkdir_p) $@
	@cp -a /usr/share/asciidoc/javascripts/asciidoc*.js $@
	@cp -a /usr/share/asciidoc/javascripts/toc.js $@

endif

include $(top_srcdir)/doc/install-dist.rules

install-data-local: install-docs-local

dist-hook: dist-docs-hook

uninstall-local: uninstall-docs

clean-local:
	$(RM) -R $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(tmpdir)

distclean-local: clean-local
