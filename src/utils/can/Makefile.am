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
	../../posix/libpthread_rt.la

rtcanrecv_SOURCES = rtcanrecv.c

rtcanrecv_LDADD = \
	../../native/libnative.la \
	../../posix/libpthread_rt.la \
	../../rtdm/librtdm.la

rtcansend_SOURCES = rtcansend.c

rtcansend_LDADD = \
	../../native/libnative.la \
	../../posix/libpthread_rt.la \
	../../rtdm/librtdm.la
