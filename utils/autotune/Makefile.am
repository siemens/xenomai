
CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

sbin_PROGRAMS = autotune

autotune_SOURCES = autotune.c

autotune_CPPFLAGS = 		\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

autotune_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

autotune_LDADD =			\
	../../lib/cobalt/libcobalt.la	\
	 @XENO_USER_LDADD@		\
	-lpthread -lrt
