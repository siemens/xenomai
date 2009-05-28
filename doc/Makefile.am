SUBDIRS=docbook doxygen man txt

GENERATE_DOC=$(top_srcdir)/scripts/maint/generate-doc

generate-doc:
	@if test \! -e $(GENERATE_DOC) -o \! -e $(srcdir)/.git ; then \
		echo "$@ may only be run from a GIT working tree." ; \
	else \
		srcdir=$(srcdir) $(GENERATE_DOC) ; \
	fi
