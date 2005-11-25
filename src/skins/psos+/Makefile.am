
lib_LIBRARIES = libpsos.a

nodist_libpsos_a_SOURCES = \
	module.c \
	asr.c \
	event.c \
	pt.c \
	queue.c \
	rn.c \
	task.c \
	tm.c \
	sem.c

AM_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include \
	-D__XENO_UVM__

CLEANFILES = $(nodist_libpsos_a_SOURCES)

$(nodist_libpsos_a_SOURCES): Makefile
	$(LN_S) -f $(top_srcdir)/ksrc/skins/psos+/$@
