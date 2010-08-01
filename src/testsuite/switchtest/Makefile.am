rundir = $(exec_prefix)/share/xenomai/testsuite/switchtest
testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = switchtest

switchtest_SOURCES = switchtest.c

switchtest_CPPFLAGS = -I$(top_srcdir)/include/posix $(XENO_USER_CFLAGS) -g -I$(top_srcdir)/include

switchtest_LDFLAGS =  $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

switchtest_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(rundir)
	@sed -e's,@XENO_TEST_DIR\@,$(XENO_TEST_DIR),g' $(srcdir)/runinfo.in > $(DESTDIR)$(rundir)/.runinfo
	@echo "#!/bin/sh" > $(DESTDIR)$(rundir)/run
	@echo "\$${DESTDIR}$(exec_prefix)/bin/xeno-load \`dirname \$$0\` \$$*" >> $(DESTDIR)$(rundir)/run
	@chmod +x $(DESTDIR)$(rundir)/run

uninstall-local:
	$(RM) $(DESTDIR)$(rundir)/.runinfo $(DESTDIR)$(rundir)/run

run: all
	@$(top_srcdir)/scripts/xeno-load --verbose

EXTRA_DIST = runinfo.in
