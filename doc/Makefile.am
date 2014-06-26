SUBDIRS=doxygen asciidoc

RESYNC_SCRIPT=$(top_srcdir)/scripts/maint/resync-doc

doxygen asciidoc: FORCE
	$(MAKE) -C $@

resync prebuild: FORCE
if XENO_BUILD_DOC
	@if test \! -e $(RESYNC_SCRIPT) -o \! -e $(top_srcdir)/.git ; then \
		echo "$@ wants $top_srcdir to be a GIT working tree." ; \
	else \
		srcdir=$(srcdir) $(RESYNC_SCRIPT) ; \
	fi
else
	@echo "build tree not configured for regenerating documentation"
	@echo "reconfigure with --enable-doc-build"
	@/bin/false
endif

.PHONY: FORCE
