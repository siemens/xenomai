testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = cyclictest

cyclictest_SOURCES = cyclictest.c

cyclictest_CPPFLAGS =			\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include/posix	\
	-I$(top_srcdir)/include/cobalt	\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes -DIPIPE_TRACE=1

cyclictest_LDFLAGS = $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

cyclictest_LDADD = \
	../../lib/cobalt/libcobalt.la \
	-lpthread -lrt
