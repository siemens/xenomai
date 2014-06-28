
noinst_LIBRARIES = librtdm.a

librtdm_a_SOURCES = rtdm.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

librtdm_a_CPPFLAGS = 		\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include
