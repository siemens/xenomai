sbin_PROGRAMS = hdb

hdb_SOURCES = hdb.c

core_libs =
if XENO_COBALT
core_libs += ../../lib/cobalt/libcobalt.la
endif

hdb_CPPFLAGS = 			\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

hdb_LDFLAGS=-Wl,--wrap=main \
	-Wl,--dynamic-list=$(top_srcdir)/scripts/dynlist.ld

hdb_LDADD =					\
	../../lib/copperplate/libcopperplate.la	\
	$(core_libs)				\
	 @XENO_USER_LDADD@			\
	-lpthread -lrt
