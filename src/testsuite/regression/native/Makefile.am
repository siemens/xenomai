tstdir = $(pkgdatadir)/regression/native

noinst_HEADERS = check.h

tst_PROGRAMS = leaks tsc mayday heap

CPPFLAGS = $(XENO_USER_CFLAGS) \
	-I$(top_srcdir)/include

LDFLAGS = $(XENO_USER_LDFLAGS)

LDADD = \
	../../../skins/native/libnative.la \
	../../../skins/rtdm/librtdm.la \
	../../../skins/common/libxenomai.la \
	-lpthread -lm
