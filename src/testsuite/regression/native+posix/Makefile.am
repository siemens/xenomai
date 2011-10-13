tstdir = $(pkgdatadir)/regression/native+posix

tst_PROGRAMS = mq_select

INCLUDES = -I$(srcdir)/..

CPPFLAGS = $(XENO_USER_CFLAGS) \
	-I$(top_srcdir)/include/posix \
	-I$(top_srcdir)/include

LDFLAGS = $(XENO_POSIX_WRAPPERS) $(XENO_USER_LDFLAGS)

LDADD = \
	../../../skins/native/libnative.la \
	../../../skins/posix/libpthread_rt.la \
	../../../skins/rtdm/librtdm.la \
	../../../skins/common/libxenomai.la \
	-lpthread -lm
