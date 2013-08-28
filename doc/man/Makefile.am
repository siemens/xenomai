HTML_DOCS = \
	clocktest \
	cyclictest \
	irqbench \
	irqloop \
	klatency \
	latency \
	rtcanconfig \
	rtcanrecv \
	rtcansend \
	switchbench \
	switchtest \
	xeno \
	xeno-config \
	xeno-test \
	dohell

EXTRA_DIST = \
	clocktest.adoc \
	cyclictest.adoc \
	irqbench.adoc \
	irqloop.adoc \
	klatency.adoc \
	latency.adoc \
	rtcanconfig.adoc \
	rtcanrecv.adoc \
	rtcansend.adoc \
	switchbench.adoc \
	switchtest.adoc \
	xeno.adoc \
	xeno-config.adoc \
	xeno-test.adoc \
	dohell.adoc

man1_MANS = \
	clocktest.1 \
	cyclictest.1 \
	irqbench.1 \
	irqloop.1 \
	klatency.1 \
	latency.1 \
	rtcanconfig.1	\
	rtcanrecv.1 \
	rtcansend.1 \
	switchbench.1 \
	switchtest.1 \
	xeno.1 \
	xeno-config.1 \
	xeno-test.1 \
	dohell.1

if !CONFIG_XENO_ASCIIDOC
HTML_DOCSDIR = $(srcdir)/../generated/html/
PDF_DOCSDIR = $(srcdir)/../generated/pdf/
MAN_DOCSDIR = $(srcdir)/../generated/man/
else
HTML_DOCSDIR = ./
PDF_DOCSDIR = ./
MAN_DOCSDIR = ./

all-local: $(HTML_DOCS)

endif

include $(top_srcdir)/doc/asciidoc/asciidoc.rules

install-data-local: install-docs-local

dist-hook: dist-docs-hook

uninstall-local: uninstall-docs

clean-local:
	$(RM) -R $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(tmpdir)

distclean-local: clean-local
