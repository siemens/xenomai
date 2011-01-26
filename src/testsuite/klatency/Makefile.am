rundir = $(exec_prefix)/share/xenomai/testsuite/klatency
testdir = @XENO_TEST_DIR@

test_PROGRAMS = klatency

klatency_SOURCES = klatency.c

klatency_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

klatency_LDFLAGS = @XENO_USER_LDFLAGS@
