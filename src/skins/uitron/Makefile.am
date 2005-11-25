
lib_LIBRARIES = libuitron.a

nodist_libuitron_a_SOURCES = \
	flag.c \
	mbx.c \
	module.c \
	sem.c \
	task.c

AM_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include \
	-D__XENO_UVM__

CLEANFILES = $(nodist_libuitron_a_SOURCES)

$(nodist_libuitron_a_SOURCES): Makefile
	$(LN_S) -f $(top_srcdir)/ksrc/skins/uitron/$@
