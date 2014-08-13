
noinst_LIBRARIES = libbufp.a

libbufp_a_SOURCES = bufp.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

libbufp_a_CPPFLAGS = 		\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include
