testdir = $(prefix)/testsuite/latency

test_PROGRAMS = latency

latency_SOURCES = latency.c

latency_CPPFLAGS = @XENO_USER_CFLAGS@

latency_LDFLAGS = @XENO_USER_LDFLAGS@

latency_LDADD = \
	../../skins/native/libnative.la \
	-lpthread -lm

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
