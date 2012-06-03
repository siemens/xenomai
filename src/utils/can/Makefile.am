sbin_PROGRAMS = rtcanconfig

bin_PROGRAMS = rtcanrecv rtcansend

CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

LDFLAGS = \
	@XENO_USER_LDFLAGS@

rtcanconfig_SOURCES = rtcanconfig.c

rtcanconfig_LDADD = \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	 -lpthread -lrt

rtcanrecv_SOURCES = rtcanrecv.c

rtcanrecv_LDADD = \
	../../skins/native/libnative.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	 -lpthread -lrt

rtcansend_SOURCES = rtcansend.c

rtcansend_LDADD = \
	../../skins/native/libnative.la \
	../../skins/rtdm/librtdm.la \
	../../skins/common/libxenomai.la \
	 -lpthread -lrt
