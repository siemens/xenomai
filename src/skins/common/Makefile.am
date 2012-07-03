lib_LTLIBRARIES = libxenomai.la
noinst_HEADERS = \
	sem_heap.h \
	internal.h

libxenomai_la_SOURCES = \
	assert_context.c \
	bind.c \
	current.c \
	rt_print.c \
	sem_heap.c \
	sigshadow.c \
	timeconv.c \
	wrappers.c

libxenomai_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 0:0:0 \
	-lpthread -lrt

libxenomai_la_CPPFLAGS = \
	@XENO_LIB_CFLAGS@ \
	-I$(top_srcdir)/include
