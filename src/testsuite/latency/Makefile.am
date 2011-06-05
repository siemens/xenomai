testdir = @XENO_TEST_DIR@

test_PROGRAMS = latency

latency_SOURCES = latency.c

latency_CPPFLAGS = \
	@XENO_USER_CFLAGS@ -Wno-missing-prototypes \
	-I$(top_srcdir)/include

latency_LDFLAGS = @XENO_USER_LDFLAGS@

latency_LDADD = \
	../../native/libnative.la \
	../../rtdm/librtdm.la \
	../../common/libxenomai.la \
	-lpthread -lm
