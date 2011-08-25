testdir = @XENO_TEST_DIR@

test_PROGRAMS = latency

latency_SOURCES = latency.c

latency_CPPFLAGS = 			\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include/cobalt	\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

latency_LDFLAGS = @XENO_USER_LDFLAGS@

latency_LDADD = \
	../../lib/native/libnative.la \
	../../lib/rtdm/librtdm.la \
	../../lib/cobalt/libcobalt.la \
	-lpthread -lrt -lm
