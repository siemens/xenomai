testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = latency

latency_SOURCES = latency.c

latency_CPPFLAGS = 		\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

latency_LDFLAGS = $(XENO_POSIX_WRAPPERS)

core_libs =
if XENO_COBALT
core_libs += ../../lib/cobalt/libcobalt.la
endif

latency_LDADD =			\
	$(core_libs)		\
	 @XENO_USER_LDADD@	\
	-lpthread -lrt -lm
