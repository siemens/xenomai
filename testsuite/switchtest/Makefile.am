testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = switchtest

switchtest_SOURCES = switchtest.c

switchtest_CPPFLAGS =			\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include

switchtest_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

switchtest_LDADD = 		\
	@XENO_CORE_LDADD@	\
	@XENO_USER_LDADD@	\
	-lpthread -lrt
