tstdir = @XENO_TEST_DIR@/regression/posix

noinst_HEADERS = check.h

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

tst_PROGRAMS = leaks shm mprotect nano_test xddp_test test_pip_exit

CPPFLAGS = $(XENO_USER_CFLAGS) \
	-I$(top_srcdir)/include/posix \
	-I$(top_srcdir)/include

LDFLAGS = $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

LDADD = \
	../../../skins/posix/libpthread_rt.la \
	../../../skins/common/libxenomai.la \
	-lpthread -lrt -lm
