HTML_DOCS = \
	clocktest \
	cyclictest \
	latency \
	rtcanconfig \
	rtcanrecv \
	rtcansend \
	switchtest \
	xeno \
	xeno-config \
	xeno-test \
	dohell

EXTRA_DIST = \
	clocktest.adoc \
	cyclictest.adoc \
	latency.adoc \
	rtcanconfig.adoc \
	rtcanrecv.adoc \
	rtcansend.adoc \
	switchtest.adoc \
	xeno.adoc \
	xeno-config.adoc \
	xeno-test.adoc \
	dohell.adoc

man1_MANS = \
	clocktest.1 \
	cyclictest.1 \
	latency.1 \
	rtcanconfig.1	\
	rtcanrecv.1 \
	rtcansend.1 \
	switchtest.1 \
	xeno.1 \
	xeno-config.1 \
	xeno-test.1 \
	dohell.1

if !XENO_BUILD_DOC
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
