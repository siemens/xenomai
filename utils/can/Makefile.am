sbin_PROGRAMS = rtcanconfig

bin_PROGRAMS = rtcanrecv rtcansend

CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

LDFLAGS =

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
