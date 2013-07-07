testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = cyclictest

cyclictest_SOURCES = cyclictest.c

cyclictest_CPPFLAGS =					\
	$(XENO_USER_CFLAGS)				\
	-I$(top_srcdir)/include				\
	-DIPIPE_TRACE=1

cyclictest_LDFLAGS = $(XENO_POSIX_WRAPPERS)

cyclictest_LDADD =			\
	../../lib/cobalt/libcobalt.la	\
	 @XENO_USER_LDADD@		\
	-lpthread -lrt
