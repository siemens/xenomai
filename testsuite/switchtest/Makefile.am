testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = switchtest

switchtest_SOURCES = switchtest.c

switchtest_CPPFLAGS =			\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

switchtest_LDFLAGS =  $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

switchtest_LDADD = \
	../../lib/cobalt/libcobalt.la \
	-lpthread -lrt
