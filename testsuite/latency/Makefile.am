testdir = @XENO_TEST_DIR@

test_PROGRAMS = latency

latency_SOURCES = latency.c

latency_CPPFLAGS = 			\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include		\
	-Wno-missing-prototypes 

latency_LDFLAGS = @XENO_USER_LDFLAGS@

core_libs =
if XENO_COBALT
core_libs += ../../lib/cobalt/libcobalt.la
endif

latency_LDADD = \
	../../lib/alchemy/libalchemy.la		\
	../../lib/copperplate/libcopperplate.la	\
	$(core_libs)				\
	-lpthread -lrt -lm
