
lib_LIBRARIES = libvxworks.a

nodist_libvxworks_a_SOURCES = \
	errnoLib.c \
	intLib.c \
	kernelLib.c \
	module.c \
	msgQLib.c \
	semLib.c \
	sysLib.c \
	taskHookLib.c \
	taskInfo.c \
	taskLib.c \
	tickLib.c \
	wdLib.c

AM_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include \
	-D__XENO_UVM__

CLEANFILES = $(nodist_libvxworks_a_SOURCES)

libvxworks.a: defs.h

$(nodist_libvxworks_a_SOURCES) defs.h: Makefile
	$(LN_S) -f $(top_srcdir)/ksrc/skins/vxworks/$@
