testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = spitest

spitest_SOURCES = spitest.c

spitest_CPPFLAGS = 		\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

spitest_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

spitest_LDADD =			\
	../../lib/smokey/libsmokey.la	\
	../../lib/@XENO_CORE_LIB@	\
	 @XENO_USER_LDADD@		\
	-lpthread -lrt
