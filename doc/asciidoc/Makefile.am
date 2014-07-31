HTML_DOCS = 					\
	html/clocktest				\
	html/slackspot				\
	html/dohell				\
	html/latency				\
	html/rtcanconfig			\
	html/rtcanrecv				\
	html/rtcansend				\
	html/switchtest				\
	html/xeno-config			\
	html/xeno-test				\
	html/xeno				\
	html/README.INSTALL			\
	html/README.APPLICATIONS		\
	html/TROUBLESHOOTING.COBALT		\
	html/TROUBLESHOOTING.MERCURY		\
	html/MIGRATION				\
	html/asciidoc-icons			\
	html/asciidoc-icons/callouts

PDF_DOCS = 				\
	README.INSTALL.pdf 		\
	README.APPLICATIONS.pdf 	\
	TROUBLESHOOTING.COBALT.pdf	\
	TROUBLESHOOTING.MERCURY.pdf	\
	MIGRATION.pdf

TXT_DOCS =				\
	README.INSTALL.txt		\
	README.APPLICATIONS.txt 	\
	TROUBLESHOOTING.COBALT.txt	\
	TROUBLESHOOTING.MERCURY.txt	\
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
	dohell.1	\
	slackspot.1

EXTRA_DIST :=				\
	README.INSTALL.adoc 		\
	README.APPLICATIONS.adoc	\
	TROUBLESHOOTING.COBALT.adoc	\
	TROUBLESHOOTING.MERCURY.adoc 	\
	MIGRATION.adoc 			\
	plaintext.conf 			\
	plaintext_postproc.awk		\
	plaintext.xsl			\
	$(MAN_DOCS:%.1=%.adoc)

if !XENO_BUILD_DOC
HTML_DOCSDIR = $(srcdir)/../prebuilt/
PDF_DOCSDIR = $(srcdir)/../prebuilt/pdf/
MAN_DOCSDIR = $(srcdir)/../prebuilt/man/
else
HTML_DOCSDIR = ./
PDF_DOCSDIR = ./
MAN_DOCSDIR = ./

ASCIIDOC_HTML_OPTS=-a icons -a iconsdir=../asciidoc-icons \
	-a toc -a toclevels=3 -a max-width=55em -a xenover=$(PACKAGE_VERSION)

ASCIIDOC_PDF_OPTS=-a icons -a toc -a toclevels=3 -a xenover=$(PACKAGE_VERSION)

ASCIIDOC_MAN_OPTS=-a xenover=$(PACKAGE_VERSION)

ASCIIDOC_TXT_OPTS=-a xenover=$(PACKAGE_VERSION) -a encoding=ascii

tmpdir=adoc_plaintext

all-local: $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(MAN_DOCS)

html/%: %.adoc Makefile
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

html/asciidoc-icons html/asciidoc-icons/callouts:
	@$(RM) -R asciidoc-icons
	@test -d /usr/share/asciidoc/images && \
	cp -a /usr/share/asciidoc/images/icons html/asciidoc-icons || \
	cp -a /usr/share/asciidoc/icons html/asciidoc-icons

endif

include $(top_srcdir)/doc/install-dist.rules

if XENO_INSTALL_DOC
install-data-local: install-docs-local
uninstall-local: uninstall-docs
else
install-data-local:
uninstall-local:
endif

dist-hook: dist-docs-hook

distclean-local: clean-local

clean-local:
	$(RM) -R $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(tmpdir)
