demodir = @XENO_DEMO_DIR@

demo_PROGRAMS = latency

core_libs =
if XENO_COBALT
core_libs += ../../lib/cobalt/libcobalt.la
SUBDIRS = cobalt
endif

cppflags = 				\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include

ldadd = 					\
	../../lib/alchemy/libalchemy.la		\
	../../lib/copperplate/libcopperplate.la	\
	$(core_libs)				\
	 @XENO_USER_LDADD@			\
	-lpthread -lrt

latency_SOURCES = latency.c
latency_CPPFLAGS = $(cppflags)
latency_LDADD = $(ldadd) -lm
