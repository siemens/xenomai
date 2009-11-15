noinst_LTLIBRARIES = libxeno_common.la
noinst_HEADERS = sem_heap.h

libxeno_common_la_SOURCES = bind.c current.c

if CONFIG_XENO_FASTSYNCH
libxeno_common_la_SOURCES += sem_heap.c
endif

libxeno_common_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include
