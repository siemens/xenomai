testdir = $(prefix)/testsuite/irqbench

test_PROGRAMS = irqloop

if XENO_TARGET_ARCH_I386
test_PROGRAMS += irqbench
endif


irqloop_SOURCES = irqloop.c

irqloop_CPPFLAGS = \
	-I$(top_srcdir)/include/posix \
	$(XENO_USER_CFLAGS) \
	-I$(top_srcdir)/include

irqloop_LDFLAGS = \
	$(XENO_POSIX_WRAPPERS) \
	$(XENO_USER_LDFLAGS)

irqloop_LDADD = \
	../../skins/posix/.libs/libpthread_rt.a -lpthread -lrt


irqbench_SOURCES = irqbench.c

irqbench_CPPFLAGS = \
	$(XENO_USER_CFLAGS) \
	-I$(top_srcdir)/include

irqbench_LDFLAGS = $(XENO_USER_LDFLAGS)


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
