demodir = @XENO_DEMO_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)
core_libs =

if XENO_COBALT
SUBDIRS = cobalt
core_libs += ../../lib/cobalt/libcobalt.la
endif

demo_PROGRAMS = 	\
	can-rtt

cppflags = 			\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

ldflags = $(XENO_POSIX_WRAPPERS)

ldadd = 					\
	$(core_libs)				\
	 @XENO_USER_LDADD@			\
	-lpthread -lrt

can_rtt_SOURCES = can-rtt.c
can_rtt_CPPFLAGS = $(cppflags)
can_rtt_LDFLAGS = $(ldflags)
can_rtt_LDADD = $(ldadd)
