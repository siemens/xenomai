
CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

sbin_PROGRAMS = corectl

corectl_SOURCES = corectl.c

corectl_CPPFLAGS = 		\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

corectl_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

corectl_LDADD =					\
	../../lib/cobalt/libcobalt.la		\
	 @XENO_USER_LDADD@			\
	-lpthread -lrt
