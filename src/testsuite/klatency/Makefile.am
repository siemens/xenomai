rundir = $(exec_prefix)/share/xenomai/testsuite/klatency
testdir = @XENO_TEST_DIR@

test_PROGRAMS = klatency

klatency_SOURCES = klatency.c

klatency_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

klatency_LDFLAGS = @XENO_USER_LDFLAGS@

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
