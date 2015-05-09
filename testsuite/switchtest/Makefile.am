testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = switchtest

switchtest_SOURCES = switchtest.c

switchtest_CPPFLAGS =			\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include

switchtest_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

switchtest_LDADD = 			\
	../../lib/@XENO_CORE_LIB@	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt
