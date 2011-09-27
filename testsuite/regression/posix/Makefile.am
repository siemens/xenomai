testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

noinst_HEADERS = check.h

test_PROGRAMS = leaks shm

CPPFLAGS = $(XENO_USER_CFLAGS) \
	-I$(top_srcdir)/include

LDFLAGS = $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

LDADD = \
	../../../lib/cobalt/libcobalt.la \
	-lpthread -lrt -lm
