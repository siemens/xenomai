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
