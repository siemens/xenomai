
noinst_LIBRARIES = libfork-exec.a

libfork_exec_a_SOURCES = fork-exec.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

libfork_exec_a_CPPFLAGS = 	\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include
