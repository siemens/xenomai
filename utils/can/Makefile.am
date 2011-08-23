sbin_PROGRAMS = rtcanconfig

bin_PROGRAMS = rtcanrecv rtcansend

CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

LDFLAGS = \
	@XENO_USER_LDFLAGS@

rtcanconfig_SOURCES = rtcanconfig.c

rtcanconfig_LDADD = \
	../../lib/rtdm/librtdm.la \
	../../lib/posix/libpthread_rt.la

rtcanrecv_SOURCES = rtcanrecv.c

rtcanrecv_LDADD = \
	../../lib/native/libnative.la \
	../../lib/posix/libpthread_rt.la \
	../../lib/rtdm/librtdm.la

rtcansend_SOURCES = rtcansend.c

rtcansend_LDADD = \
	../../lib/native/libnative.la \
	../../lib/posix/libpthread_rt.la \
	../../lib/rtdm/librtdm.la
