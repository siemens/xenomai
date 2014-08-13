
noinst_LIBRARIES = libxddp.a

libxddp_a_SOURCES = xddp.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

libxddp_a_CPPFLAGS = 		\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include
