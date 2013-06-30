noinst_LTLIBRARIES = libsysdeps.la

libsysdeps_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0

libsysdeps_la_SOURCES =	\
	features.c	\
	syscall.h	\
	tsc.h

libsysdeps_la_CPPFLAGS =				\
	@XENO_COBALT_CFLAGS@ -Wno-missing-prototypes	\
	-I$(srcdir)/../..				\
	-I$(top_srcdir)/include/cobalt			\
	-I$(top_srcdir)/include
