HTML_DOCS=api

EXTRA_DIST = \
	Doxyfile.in

if !CONFIG_XENO_DOC_DOX
HTML_DOCSDIR = $(srcdir)/../generated/html/
else
HTML_DOCSDIR = ./

# Doxygen documentation
api: doxygen-api

all-local: api

doxygen-api: Doxyfile
	$(DOXYGEN) $(DOXYDOC_OPTS)

distclean-local:
	for dir in $(HTML_DOCS) ; do \
	    if test -d $$dir ; then $(RM) -R $$dir ; fi ; \
	done

.PHONY: doxygen-api

.DELETE_ON_ERROR:

endif

include $(top_srcdir)/doc/install-dist.rules

install-data-local: install-docs-local

dist-hook: dist-docs-hook

uninstall-local: uninstall-docs
