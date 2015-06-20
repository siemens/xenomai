
noinst_LIBRARIES = libleaks.a

libleaks_a_SOURCES = leaks.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

libleaks_a_CPPFLAGS = 		\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include
