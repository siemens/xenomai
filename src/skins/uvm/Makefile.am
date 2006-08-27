lib_LTLIBRARIES = libuvm.la

libuvm_la_LDFLAGS = -version-info 0:0:0 -lpthread

libuvm_la_SOURCES = \
	init.c \
	uvm.c

libuvm_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

SUBDIRS = nucleus vxworks psos+ vrtx uitron
