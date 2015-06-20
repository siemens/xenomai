
noinst_LIBRARIES = libposix-select.a

libposix_select_a_SOURCES = posix-select.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

libposix_select_a_CPPFLAGS = 	\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include
