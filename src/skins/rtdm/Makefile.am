lib_LTLIBRARIES = librtdm.la
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libxenomai_rtdm.pc

librtdm_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 1:0:0 -lpthread

librtdm_la_SOURCES = \
	core.c \
	init.c

librtdm_la_CPPFLAGS = \
	@XENO_LIB_CFLAGS@ \
	-I$(top_srcdir)/include
