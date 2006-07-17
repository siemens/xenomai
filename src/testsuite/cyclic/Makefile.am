testdir = $(prefix)/testsuite/cyclic

test_PROGRAMS = cyclictest

cyclictest_SOURCES = cyclictest.c

cyclictest_CPPFLAGS = -I$(top_srcdir)/include/posix $(XENO_USER_CFLAGS) -DIPIPE_TRACE=1 -I$(top_srcdir)/include

cyclictest_LDFLAGS = $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

cyclictest_LDADD = \
	-lpthread -lrt \
	../../skins/posix/.libs/libpthread_rt.a

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(testdir)
	$(INSTALL_DATA) $(srcdir)/runinfo $(DESTDIR)$(testdir)/.runinfo
	@echo "\$${DESTDIR}$(prefix)/bin/xeno-load \$$*" > $(DESTDIR)$(testdir)/run
	@chmod +x $(DESTDIR)$(testdir)/run

uninstall-local:
	$(RM) $(DESTDIR)$(testdir)/.runinfo $(DESTDIR)$(testdir)/run

run: all
	@$(top_srcdir)/scripts/xeno-load --verbose

EXTRA_DIST = runinfo
