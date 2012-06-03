rundir = $(exec_prefix)/share/xenomai/testsuite/latency
testdir = @XENO_TEST_DIR@

test_PROGRAMS = latency

latency_SOURCES = latency.c

latency_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

latency_LDFLAGS = @XENO_USER_LDFLAGS@

latency_LDADD = \
	../../skins/native/libnative.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt -lm
