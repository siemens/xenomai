
noinst_LTLIBRARIES = libboilerplate.la

libboilerplate_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -lpthread -version-info 0:0:0

libboilerplate_la_SOURCES =	\
	ancillaries.c		\
	hash.c			\
	obstack.c		\
	time.c

if XENO_DEBUG
libboilerplate_la_SOURCES += debug.c
endif

nodist_libboilerplate_la_SOURCES = config-dump.h
BUILT_SOURCES = config-dump.h
CLEANFILES = config-dump.h

config-dump.h: Makefile $(CONFIG_HEADER)
	( sed -e '/^#define CONFIG_/!d' \
	-e 's/^#[\t ]*define \(CONFIG_[^ ]*\) \(.*\)/\1=\2/' \
	-e 's,\",\\&,g' \
	-e 's/^.*$$/\"&\",/' $(CONFIG_HEADER); echo '"---",'; \
	sed -e '/#undef CONFIG_/!d' \
	-e 's/.*\(CONFIG_[^ ]*\).*/\1/' \
	-e 's/^.*$$/\"& is OFF\",/' $(CONFIG_HEADER) ) > $@

libboilerplate_la_CPPFLAGS =				\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/include

libboilerplate_la_LIBADD = libiniparser.la
noinst_LTLIBRARIES += libiniparser.la

libiniparser_la_SOURCES	=	\
	iniparser/dictionary.c	\
	iniparser/dictionary.h	\
	iniparser/iniparser.h	\
	iniparser/iniparser.c

libiniparser_la_CPPFLAGS	=		\
	-Iiniparser @XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include

EXTRA_DIST = iniparser/README iniparser/LICENSE

# We always build the tlsf/malloc support. In the pshared case, it
# will provide for private memory allocation.
if XENO_TLSF
libboilerplate_la_LIBADD +=	libtlsf.la
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

EXTRA_DIST += tlsf/README

SPARSE = sparse

sparse:
	@for i in $(libboilerplate_la_SOURCES) $(libtlsf_la_SOURCES) $(libiniparser_la_SOURCES; do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done
