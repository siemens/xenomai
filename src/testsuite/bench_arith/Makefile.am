testdir = $(exec_prefix)/share/xenomai/testsuite/bench_arith

bin_PROGRAMS = bench_arith

bench_arith_SOURCES = bench_arith.c outofline.c outofline.h

bench_arith_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

bench_arith_LDFLAGS = @XENO_USER_LDFLAGS@

bench_arith_LDADD = \
	../../skins/native/libnative.la \
	-lpthread -lm

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
