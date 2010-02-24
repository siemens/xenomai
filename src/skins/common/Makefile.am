lib_LTLIBRARIES = libxenomai.la
noinst_HEADERS = sem_heap.h

libxenomai_la_SOURCES = bind.c current.c sigshadow.c timeconv.c

if CONFIG_XENO_FASTSYNCH
libxenomai_la_SOURCES += sem_heap.c
endif

libxenomai_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 0:0:0 -lpthread

libxenomai_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include
