SUBDIRS=doxygen asciidoc man

RESYNC_SCRIPT=$(top_srcdir)/scripts/maint/resync-doc

doxygen asciidoc man: FORCE
	$(MAKE) -C $@

resync prebuild: FORCE
	@if test \! -e $(RESYNC_SCRIPT) -o \! -e $(top_srcdir)/.git ; then \
		echo "$@ wants $top_srcdir to be a GIT working tree." ; \
	else \
		srcdir=$(srcdir) $(RESYNC_SCRIPT) ; \
	fi

.PHONY: FORCE
