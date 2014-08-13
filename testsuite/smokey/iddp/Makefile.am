
noinst_LIBRARIES = libiddp.a

libiddp_a_SOURCES = iddp.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

libiddp_a_CPPFLAGS = 		\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include
