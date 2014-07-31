SUBDIRS=gitdoc doxygen asciidoc

RESYNC_SCRIPT=$(top_srcdir)/scripts/maint/resync-doc

gitdoc doxygen asciidoc: FORCE
	$(MAKE) -C $@

if XENO_BUILD_DOC
resync prebuild: $(SUBDIRS)
	@srcdir=$(srcdir) $(RESYNC_SCRIPT)
else
resync prebuild: FORCE
	@echo "build tree not configured for regenerating documentation"
	@echo "reconfigure with --enable-doc-build"
	@/bin/false
endif

.PHONY: FORCE
