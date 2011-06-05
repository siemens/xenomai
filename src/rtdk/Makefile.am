lib_LTLIBRARIES = librtdk.la

librtdk_la_LDFLAGS = -version-info 0:0:0 -lpthread -lrt

librtdk_la_SOURCES = \
	init.c \
	rt_print.c \
	assert_context.c \
	wrappers.c

librtdk_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ -Wno-missing-prototypes \
	-I$(top_srcdir)/include

noinst_HEADERS = internal.h
