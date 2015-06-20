
noinst_LIBRARIES = libtimerfd.a

libtimerfd_a_SOURCES = timerfd.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

libtimerfd_a_CPPFLAGS = 	\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include
