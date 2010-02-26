rundir = $(exec_prefix)/share/xenomai/testsuite/clocktest
testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = clocktest

clocktest_SOURCES = clocktest.c

clocktest_CPPFLAGS = -I$(top_srcdir)/include/posix $(XENO_USER_CFLAGS) -I$(top_srcdir)/include

clocktest_LDFLAGS = $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

clocktest_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(rundir)
	@sed -e's,@exec_prefix\@,$(exec_prefix),g' $(srcdir)/runinfo.in > $(DESTDIR)$(rundir)/.runinfo
	@echo "#!/bin/sh" > $(DESTDIR)$(rundir)/run
	@echo "\$${DESTDIR}$(exec_prefix)/bin/xeno-load \`dirname \$$0\` \$$*" >> $(DESTDIR)$(rundir)/run
	@chmod +x $(DESTDIR)$(rundir)/run

uninstall-local:
	$(RM) $(DESTDIR)$(rundir)/.runinfo $(DESTDIR)$(rundir)/run

run: all
	@$(top_srcdir)/scripts/xeno-load --verbose

EXTRA_DIST = runinfo.in
