testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = clocktest

clocktest_SOURCES = clocktest.c

clocktest_CPPFLAGS = -I$(top_srcdir)/include/posix $(XENO_USER_CFLAGS) -I$(top_srcdir)/include -Wno-missing-prototypes 

clocktest_LDFLAGS = $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

clocktest_LDADD = \
	../../posix/libpthread_rt.la \
	../../common/libxenomai.la \
	-lpthread -lrt
