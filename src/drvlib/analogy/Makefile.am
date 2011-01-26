lib_LTLIBRARIES = libanalogy.la

libanalogy_la_LDFLAGS = -version-info 1:0:0 -lpthread

libanalogy_la_SOURCES = \
	async.c \
	descriptor.c \
	info.c \
	range.c \
	root_leaf.h \
	sync.c \
	sys.c \
	syscall.h

libanalogy_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include
