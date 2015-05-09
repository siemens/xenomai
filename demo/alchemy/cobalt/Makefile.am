demodir = @XENO_DEMO_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

demo_PROGRAMS = cross-link

cppflags = 			\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

ldadd = 						\
	../../../lib/alchemy/libalchemy.la		\
	../../../lib/copperplate/libcopperplate.la	\
	../../../lib/cobalt/libcobalt.la		\
	@XENO_AUTOINIT_LDFLAGS@				\
	$(XENO_POSIX_WRAPPERS)				\
	 @XENO_USER_LDADD@				\
	-lpthread -lrt

cross_link_SOURCES = cross-link.c
cross_link_CPPFLAGS = $(cppflags)
cross_link_LDADD = $(ldadd)
