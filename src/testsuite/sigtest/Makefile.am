rundir = $(exec_prefix)/share/xenomai/testsuite/sigtest
testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = sigtest

sigtest_SOURCES = sigtest.c

sigtest_CPPFLAGS = -I$(top_srcdir)/include/posix $(XENO_USER_CFLAGS) -g -I$(top_srcdir)/include

sigtest_LDFLAGS =  $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)
if CONFIG_XENO_SHARED
sigtest_LDFLAGS += -rdynamic
endif

sigtest_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt
