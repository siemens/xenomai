rundir = $(exec_prefix)/share/xenomai/testsuite/cyclic
testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = cyclictest

cyclictest_SOURCES = cyclictest.c

cyclictest_CPPFLAGS = -I$(top_srcdir)/include/posix $(XENO_USER_CFLAGS) -DIPIPE_TRACE=1 -I$(top_srcdir)/include

cyclictest_LDFLAGS = $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

cyclictest_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt
