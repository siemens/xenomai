sbin_PROGRAMS = rtcanconfig

bin_PROGRAMS = rtcanrecv rtcansend

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

CPPFLAGS = 						\
	@XENO_USER_CFLAGS@ 				\
	-I$(top_srcdir)/include

LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

rtcanconfig_SOURCES = rtcanconfig.c

rtcanconfig_LDADD = \
	../../lib/cobalt/libcobalt.la	\
	 @XENO_USER_LDADD@		\
	-lpthread -lrt

rtcanrecv_SOURCES = rtcanrecv.c

rtcanrecv_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt

rtcansend_SOURCES = rtcansend.c

rtcansend_LDADD = \
	../../lib/alchemy/libalchemy.la \
	../../lib/copperplate/libcopperplate.la \
	../../lib/cobalt/libcobalt.la	\
	@XENO_USER_LDADD@		\
	-lpthread -lrt
