HTML_DOCS = clocktest
EXTRA_DIST = clocktest.adoc

man1_MANS = \
	clocktest.man \
	cyclictest.man \
	irqbench.man \
	irqloop.man \
	klatency.man \
	latency.man \
	rtcanconfig.man	\
	rtcanrecv.man \
	rtcansend.man \
	switchbench.man \
	switchtest.man \
	xeno.man \
	xeno-config.man

if !CONFIG_XENO_ASCIIDOC
HTML_DOCSDIR = $(srcdir)/../generated/html/
PDF_DOCSDIR = $(srcdir)/../generated/pdf/
else
HTML_DOCSDIR = ./
PDF_DOCSDIR = ./

all-local: $(HTML_DOCS)

endif

include $(top_srcdir)/doc/asciidoc.rules

install-data-local: install-docs-local

dist-hook: dist-docs-hook

uninstall-local: uninstall-docs

clean-local:
	$(RM) -R $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(tmpdir)

distclean-local: clean-local
