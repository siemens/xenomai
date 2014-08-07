if XENO_BUILD_DOC
git-src-check: FORCE
	@if test \! -e $(top_srcdir)/.git ; then \
		echo "$@ wants $top_srcdir to be a GIT working tree." ; \
		/bin/false; \
	fi
else
git-src-check:
endif

INPUT_DOCS = 							\
	asciidoc/pages/Installing_Xenomai_3.x.adoc		\
	asciidoc/pages/Running_Apps_with_Xenomai_3.x.adoc	\
	asciidoc/pages/Migrating_to_Xenomai_3.x.adoc		\
	asciidoc/pages/Troubleshooting_dual_kernel.adoc		\
	asciidoc/pages/Troubleshooting_single_kernel.adoc

OUTPUT_DOCS = 						\
	doc/asciidoc/README.INSTALL.adoc		\
	doc/asciidoc/README.APPLICATIONS.adoc		\
	doc/asciidoc/MIGRATION.adoc			\
	doc/asciidoc/TROUBLESHOOTING.COBALT.adoc	\
	doc/asciidoc/TROUBLESHOOTING.MERCURY.adoc

all-local: git-src-check
	@set -e; if test \! x$(XENO_DOC_GIT) = x; then	\
		if test -d doc.git; then \
			(cd doc.git && git pull --quiet --force); \
		else \
			git clone --branch master --depth 1 --quiet \
			$(XENO_DOC_GIT) doc.git; \
		fi; \
		set -- $(OUTPUT_DOCS); \
		for doc in $(INPUT_DOCS); do \
			cp doc.git/$$doc $(top_srcdir)/$$1; \
			shift; \
		done; \
	fi

clean-local:
	$(RM) -R doc.git

.PHONY: FORCE
