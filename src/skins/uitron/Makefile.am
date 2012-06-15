lib_LTLIBRARIES = libuitron.la
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libxenomai_uitron.pc

libuitron_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 0:0:0 -lpthread

libuitron_la_SOURCES = \
	flag.c \
	init.c \
	mbx.c \
	sem.c \
	task.c

libuitron_la_CPPFLAGS = \
	@XENO_LIB_CFLAGS@ \
	-I$(top_srcdir)/include
