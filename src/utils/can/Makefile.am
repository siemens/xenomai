sbin_PROGRAMS = rtcanconfig

bin_PROGRAMS = rtcanrecv rtcansend


CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

LDFLAGS = \
	@XENO_USER_LDFLAGS@ \
	-L../../skins/native \
	-L../../skins/rtdm

LDADD = -lpthread -lnative -lrtdm


rtcanconfig_SOURCES = rtcanconfig.c

rtcanrecv_SOURCES = rtcanrecv.c

rtcansend_SOURCES = rtcansend.c
