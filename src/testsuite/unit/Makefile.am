testdir = $(exec_prefix)/share/xenomai/testsuite/unit

bin_PROGRAMS = arith wakeup-time mutex-torture-posix mutex-torture-native

arith_SOURCES = arith.c arith-noinline.c arith-noinline.h

arith_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

arith_LDFLAGS = @XENO_USER_LDFLAGS@

arith_LDADD = \
	../../skins/native/libnative.la \
	-lpthread -lm

wakeup_time_SOURCES = wakeup-time.c

wakeup_time_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

wakeup_time_LDFLAGS = @XENO_USER_LDFLAGS@

wakeup_time_LDADD = \
	../../skins/native/libnative.la \
	-lpthread -lm

mutex_torture_posix_SOURCES = mutex-torture.c

mutex_torture_posix_CPPFLAGS = \
	@XENO_USER_CFLAGS@ -D__POSIX_SKIN__ \
	-I$(top_srcdir)/include

mutex_torture_posix_LDFLAGS = @XENO_USER_LDFLAGS@

mutex_torture_posix_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/native/libnative.la \
	-lpthread -lrt -lm

mutex_torture_native_SOURCES = mutex-torture.c

mutex_torture_native_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

mutex_torture_native_LDFLAGS = @XENO_USER_LDFLAGS@

mutex_torture_native_LDADD = \
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
