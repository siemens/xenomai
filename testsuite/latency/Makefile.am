testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = latency

latency_SOURCES = latency.c

latency_CPPFLAGS = 		\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

latency_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

latency_LDADD =				\
	../../lib/@XENO_CORE_LIB@	\
	 @XENO_USER_LDADD@		\
	-lpthread -lrt -lm
