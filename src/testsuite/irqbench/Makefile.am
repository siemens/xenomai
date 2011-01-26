rundir = $(exec_prefix)/share/xenomai/testsuite/irqbench
testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = irqloop

if XENO_TARGET_ARCH_X86
test_PROGRAMS += irqbench
endif


irqloop_SOURCES = irqloop.c

irqloop_CPPFLAGS = \
	-I$(top_srcdir)/include/posix \
	$(XENO_USER_CFLAGS) \
	-I$(top_srcdir)/include

irqloop_LDFLAGS = \
	$(XENO_POSIX_WRAPPERS) \
	$(XENO_USER_LDFLAGS)

irqloop_LDADD = \
	../../skins/posix/libpthread_rt.la \
	../../skins/common/libxenomai.la \
	-lpthread -lrt

irqbench_SOURCES = irqbench.c

irqbench_CPPFLAGS = \
	$(XENO_USER_CFLAGS) \
	-I$(top_srcdir)/include

irqbench_LDFLAGS = $(XENO_USER_LDFLAGS)
