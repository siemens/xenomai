HTML_DOCS = html/xeno3prm html/xeno3prm/search
PDF_DOCS  = xeno3prm.pdf
EXTRA_DIST = xeno3prm-common.conf.in xeno3prm-html.conf.in xeno3prm-latex.conf.in

if !XENO_BUILD_DOC
HTML_DOCSDIR = $(srcdir)/../prebuilt/
PDF_DOCSDIR = $(srcdir)/../prebuilt/pdf/

html pdf:
	@echo "build tree not configured for regenerating documentation"
	@echo "reconfigure with --enable-doc-build"
	@/bin/false
else
HTML_DOCSDIR = ./
PDF_DOCSDIR = ./

all-local: html pdf

html/xeno3prm/search: html/xeno3prm

html: $(HTML_DOCS)

pdf: $(PDF_DOCS)

html/xeno3prm latex/xeno3prm: FORCE
	@mkdir -p $@
	$(DOXYGEN) $(@F)-$(@D).conf

%.pdf: latex/%
	$(MAKE) -C $< refman.pdf > /dev/null
	mv $</refman.pdf $@

distclean-local:
	for dir in *-html *-latex; do \
	    if test -d $$dir ; then $(RM) -R $$dir ; fi ; \
	done

.PHONY: FORCE

.DELETE_ON_ERROR:

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
