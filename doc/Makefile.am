SUBDIRS=doxygen asciidoc man

GENERATE_DOC=$(top_srcdir)/scripts/maint/generate-doc

doxygen asciidoc man: FORCE
	$(MAKE) -C $@

generate-doc:
	@if test \! -e $(GENERATE_DOC) -o \! -e $(top_srcdir)/.git ; then \
		echo "$@ wants $top_srcdir to be a GIT working tree." ; \
	else \
		srcdir=$(srcdir) $(GENERATE_DOC) ; \
	fi

.PHONY: FORCE
