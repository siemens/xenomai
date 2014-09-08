SUBDIRS=asciidoc doxygen man txt

GENERATE_DOC=$(top_srcdir)/scripts/maint/generate-doc

generate-doc:
	@if test \! -e $(GENERATE_DOC) -o \! -e $(top_srcdir)/.git ; then \
		echo "$@ wants $top_srcdir to be a GIT working tree." ; \
	else \
		srcdir=$(srcdir) $(GENERATE_DOC) ; \
	fi
