HTML_DOCS = xeno3prm-html xeno3prm-html/search
PDF_DOCS  = xeno3prm.pdf
EXTRA_DIST = xeno3prm-common.conf.in xeno3prm-html.conf.in xeno3prm-pdf.conf.in

if !XENO_BUILD_DOC
HTML_DOCSDIR = $(srcdir)/../prebuilt/html/
PDF_DOCSDIR = $(srcdir)/../prebuilt/pdf/

html pdf:
	@echo "build tree not configured for regenerating documentation"
	@echo "reconfigure with --enable-doc-build"
	@/bin/false
else
HTML_DOCSDIR = ./
PDF_DOCSDIR = ./

all-local: html pdf

html: $(HTML_DOCS)

pdf: $(PDF_DOCS)

xeno3prm-html:
	$(DOXYGEN) $@.conf

%.pdf: FORCE
	$(DOXYGEN) $*-pdf.conf
	$(MAKE) -C $*-latex refman.pdf
	mv $*-latex/refman.pdf $@

distclean-local:
	for dir in $(HTML_DOCS) *-latex; do \
	    if test -d $$dir ; then $(RM) -R $$dir ; fi ; \
	done

.PHONY: xeno3prm-html FORCE

.DELETE_ON_ERROR:

endif

include $(top_srcdir)/doc/install-dist.rules

install-data-local: install-docs-local

dist-hook: dist-docs-hook

uninstall-local: uninstall-docs
