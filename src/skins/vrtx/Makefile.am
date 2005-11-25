
lib_LIBRARIES = libvrtx.a

nodist_libvrtx_a_SOURCES = \
	event.c \
	heap.c \
	mb.c \
	module.c \
	mx.c \
	pt.c \
	queue.c \
	sem.c \
	task.c \
	time.c

AM_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include \
	-D__XENO_UVM__

CLEANFILES = $(nodist_libvrtx_a_SOURCES)

$(nodist_libvrtx_a_SOURCES): Makefile
	$(LN_S) -f $(top_srcdir)/ksrc/skins/vrtx/$@
