lib_LTLIBRARIES = libnative.la
pkgconfigdir = $(libdir)/pkgconfig

libnative_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 3:0:0 -lpthread

libnative_la_SOURCES = \
	alarm.c \
	buffer.c \
	cond.c \
	event.c \
	heap.c \
	init.c \
	mutex.c \
	queue.c \
	sem.c \
	task.c \
	timer.c \
	wrappers.c \
	wrappers.h

libnative_la_CPPFLAGS =			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include/cobalt	\
	-I$(top_srcdir)/include
