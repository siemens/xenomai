testdir = $(exec_prefix)/testsuite/switchbench

test_PROGRAMS = switchbench

switchbench_SOURCES = switchbench.c

switchbench_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

switchbench_LDFLAGS = @XENO_USER_LDFLAGS@

switchbench_LDADD = \
	../../skins/native/.libs/libnative.a \
	-lpthread -lm

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(testdir)
	$(INSTALL_DATA) $(srcdir)/runinfo $(DESTDIR)$(testdir)/.runinfo
	@echo "\$${DESTDIR}$(exec_prefix)/bin/xeno-load \$$*" > $(DESTDIR)$(testdir)/run
	@chmod +x $(DESTDIR)$(testdir)/run

uninstall-local:
	$(RM) $(DESTDIR)$(testdir)/.runinfo $(DESTDIR)$(testdir)/run

run: all
	@$(top_srcdir)/scripts/xeno-load --verbose

EXTRA_DIST = runinfo
