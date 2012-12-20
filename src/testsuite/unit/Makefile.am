rundir = $(exec_prefix)/share/xenomai/testsuite/unit
testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = \
	arith \
	wakeup-time \
	mutex-torture-posix \
	mutex-torture-native \
	cond-torture-posix \
	cond-torture-native \
	check-vdso \
	rtdm \
	sched-tp

arith_SOURCES = arith.c arith-noinline.c arith-noinline.h

arith_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

arith_LDFLAGS = @XENO_USER_LDFLAGS@

arith_LDADD = \
	../../skins/native/libnative.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm

wakeup_time_SOURCES = wakeup-time.c

wakeup_time_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

wakeup_time_LDFLAGS = @XENO_USER_LDFLAGS@

wakeup_time_LDADD = \
	../../skins/native/libnative.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm

mutex_torture_posix_SOURCES = mutex-torture.c

mutex_torture_posix_CPPFLAGS = \
	-I$(top_srcdir)/include/posix @XENO_USER_CFLAGS@ -g -DXENO_POSIX \
	-I$(top_srcdir)/include

mutex_torture_posix_LDFLAGS = $(XENO_POSIX_WRAPPERS) @XENO_USER_LDFLAGS@

mutex_torture_posix_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/native/libnative.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm

mutex_torture_native_SOURCES = mutex-torture.c

mutex_torture_native_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

mutex_torture_native_LDFLAGS = @XENO_USER_LDFLAGS@

mutex_torture_native_LDADD = \
	../../skins/native/libnative.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm

cond_torture_posix_SOURCES = cond-torture.c

cond_torture_posix_CPPFLAGS = \
	-I$(top_srcdir)/include/posix @XENO_USER_CFLAGS@ -g -DXENO_POSIX \
	-I$(top_srcdir)/include

cond_torture_posix_LDFLAGS = $(XENO_POSIX_WRAPPERS) @XENO_USER_LDFLAGS@

cond_torture_posix_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/native/libnative.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm

cond_torture_native_SOURCES = cond-torture.c

cond_torture_native_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

cond_torture_native_LDFLAGS = @XENO_USER_LDFLAGS@

cond_torture_native_LDADD = \
	../../skins/native/libnative.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm

check_vdso_SOURCES = check-vdso.c

check_vdso_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

check_vdso_LDFLAGS = @XENO_USER_LDFLAGS@

check_vdso_LDADD = \
	../../skins/native/libnative.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm

rtdm_SOURCES = rtdm.c

rtdm_CPPFLAGS = \
	-I$(top_srcdir)/include/posix @XENO_USER_CFLAGS@ -g -DXENO_POSIX \
	-I$(top_srcdir)/include

rtdm_LDFLAGS = $(XENO_POSIX_WRAPPERS) @XENO_USER_LDFLAGS@

rtdm_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/native/libnative.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm

sched_tp_SOURCES = sched-tp.c

sched_tp_CPPFLAGS = \
	-I$(top_srcdir)/include/posix @XENO_USER_CFLAGS@ -g -DXENO_POSIX \
	-I$(top_srcdir)/include

sched_tp_LDFLAGS = $(XENO_POSIX_WRAPPERS) @XENO_USER_LDFLAGS@

sched_tp_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm
