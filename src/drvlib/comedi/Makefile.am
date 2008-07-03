lib_LTLIBRARIES = libcomedi.la

libcomedi_la_LDFLAGS = -version-info 0:0:0 -lpthread

libcomedi_la_SOURCES = \
	async.c \
	descriptor.c \
	info.c \
	range.c \
	sync.c \
	sys.c

libcomedi_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include
