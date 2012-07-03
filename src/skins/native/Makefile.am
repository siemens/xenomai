lib_LTLIBRARIES = libnative.la
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libxenomai_native.pc

libnative_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 3:0:0 -lpthread

libnative_la_SOURCES = \
	alarm.c \
	buffer.c \
	cond.c \
	event.c \
	heap.c \
	init.c \
	intr.c \
	misc.c \
	mutex.c \
	pipe.c \
	queue.c \
	sem.c \
	task.c \
	timer.c \
	wrappers.c \
	wrappers.h

libnative_la_CPPFLAGS = \
	@XENO_LIB_CFLAGS@ \
	-I$(top_srcdir)/include
