testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = clocktest

clocktest_SOURCES = clocktest.c

clocktest_CPPFLAGS =					\
	$(XENO_USER_CFLAGS)				\
	-I$(top_srcdir)/include

clocktest_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

clocktest_LDADD =			\
	../../lib/cobalt/libcobalt.la	\
	 @XENO_USER_LDADD@		\
	-lpthread -lrt
