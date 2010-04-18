sbin_PROGRAMS = rtcanconfig

bin_PROGRAMS = rtcanrecv rtcansend


CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

LDFLAGS = \
	@XENO_USER_LDFLAGS@


rtcanconfig_SOURCES = rtcanconfig.c

rtcanconfig_LDADD = \
	../../rtdm/librtdm.la \
	../../common/libxenomai.la

rtcanrecv_SOURCES = rtcanrecv.c

rtcanrecv_LDADD = \
	../../native/libnative.la \
	../../rtdm/librtdm.la \
	../../common/libxenomai.la


rtcansend_SOURCES = rtcansend.c

rtcansend_LDADD = \
	../../native/libnative.la \
	../../rtdm/librtdm.la \
	../../common/libxenomai.la
