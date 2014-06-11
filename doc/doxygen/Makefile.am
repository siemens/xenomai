HTML_DOCS = manual-html manual-html/search
PDF_DOCS  = manual.pdf
EXTRA_DIST = manual-common.conf.in manual-html.conf.in manual-pdf.conf.in

if !XENO_BUILD_DOC
HTML_DOCSDIR = $(srcdir)/../generated/html/
PDF_DOCSDIR = $(srcdir)/../generated/pdf/
else
HTML_DOCSDIR = ./
PDF_DOCSDIR = ./

all-local: $(HTML_DOCS) $(PDF_DOCS)

manual-html:
	$(DOXYGEN) $@.conf

%.pdf: FORCE
	$(DOXYGEN) $*-pdf.conf
	$(MAKE) -C $*-latex refman.pdf
	mv $*-latex/refman.pdf $@

distclean-local:
	for dir in $(HTML_DOCS) *-latex; do \
	    if test -d $$dir ; then $(RM) -R $$dir ; fi ; \
	done

.PHONY: manual-html FORCE

.DELETE_ON_ERROR:

endif

include $(top_srcdir)/doc/install-dist.rules

install-data-local: install-docs-local

dist-hook: dist-docs-hook

uninstall-local: uninstall-docs
