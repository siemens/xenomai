lib_LTLIBRARIES = libpsos.la
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libxenomai_psos+.pc

libpsos_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 0:0:0 -lpthread

libpsos_la_SOURCES = \
	asr.c \
	init.c \
	queue.c \
	rn.c \
	sem.c \
	task.c \
	tm.c

libpsos_la_CPPFLAGS = \
	@XENO_LIB_CFLAGS@ \
	-I$(top_srcdir)/include
