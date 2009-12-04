testdir = $(exec_prefix)/share/xenomai/testsuite/sigtest

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

bin_PROGRAMS = sigtest

sigtest_SOURCES = sigtest.c

sigtest_CPPFLAGS = -I$(top_srcdir)/include/posix $(XENO_USER_CFLAGS) -g -I$(top_srcdir)/include

sigtest_LDFLAGS =  $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)
if CONFIG_XENO_SHARED
sigtest_LDFLAGS += -rdynamic
endif

sigtest_LDADD = \
	../../skins/posix/libpthread_rt.la -lpthread -lrt

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(testdir)
	@sed -e's,@exec_prefix\@,$(exec_prefix),g' $(srcdir)/runinfo.in > $(DESTDIR)$(testdir)/.runinfo
	@echo "#!/bin/sh" > $(DESTDIR)$(testdir)/run
	@echo "\$${DESTDIR}$(exec_prefix)/bin/xeno-load \`dirname \$$0\` \$$*" >> $(DESTDIR)$(testdir)/run
	@chmod +x $(DESTDIR)$(testdir)/run

uninstall-local:
	$(RM) $(DESTDIR)$(testdir)/.runinfo $(DESTDIR)$(testdir)/run

run: all
	@$(top_srcdir)/scripts/xeno-load --verbose

EXTRA_DIST = runinfo.in
