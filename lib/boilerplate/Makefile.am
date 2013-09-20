
noinst_LTLIBRARIES = libboilerplate.la

libboilerplate_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0

libboilerplate_la_SOURCES =	\
	ancillaries.c		\
	hash.c			\
	time.c

if XENO_DEBUG
libboilerplate_la_SOURCES += debug.c
endif

libboilerplate_la_CPPFLAGS =				\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/include

# We always build the tlsf/malloc support. In the pshared case, it
# will provide for private memory allocation.
if XENO_TLSF
libboilerplate_la_LIBADD =	libtlsf.la
noinst_LTLIBRARIES += libtlsf.la
endif

libtlsf_la_SOURCES =	\
	tlsf/tlsf.c	\
	tlsf/tlsf.h	\
	tlsf/target.h

libtlsf_la_CPPFLAGS =					\
	-Itlsf @XENO_USER_CFLAGS@			\
	-I$(top_srcdir)/include				\
	-DTLSF_USE_LOCKS=1 -DUSE_MMAP=1 -DTLSF_STATISTIC=1

EXTRA_DIST = tlsf/README

SPARSE = sparse

sparse:
	@for i in $(libboilerplate_la_SOURCES) $(libtlsf_la_SOURCES); do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done
