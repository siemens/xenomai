
noinst_LTLIBRARIES = libavl.la libversion.la libiniparser.la libboilerplate.la
libboilerplate_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -lpthread
libboilerplate_la_LIBADD = libavl.la libversion.la libiniparser.la

libboilerplate_la_SOURCES =	\
	ancillaries.c		\
	heapmem.c		\
	hash.c			\
	setup.c			\
	time.c

if XENO_PRIVATE_OBSTACK
libboilerplate_la_SOURCES += obstack.c
endif

if XENO_DEBUG
libboilerplate_la_SOURCES += debug.c
endif

libversion_la_CFLAGS = @XENO_USER_CFLAGS@
libversion_la_SOURCES = version.c
version.c: git-stamp.h

git-stamp.h: git-stamp
	@if test -r $(top_srcdir)/.git; then						\
	  stamp=`git --git-dir=$(top_srcdir)/.git rev-list --abbrev-commit -1 HEAD`;	\
	  if test \! -s $@ || grep -wvq $$stamp $@; then				\
		date=`git --git-dir=$(top_srcdir)/.git log -1 $$stamp --pretty=format:%ci`;	\
		echo "#define GIT_STAMP \"#$$stamp ($$date)\"" > $@;			\
	  fi;										\
	elif test \! -r $@ -o -s $@; then						\
	    rm -f $@ && touch $@;							\
	fi; true

sbin_PROGRAMS = version
version_CFLAGS = @XENO_USER_CFLAGS@ -I$(top_srcdir)/include -D__PROGRAM__
version_SOURCES = version.c

clean-local:
	$(RM) git-stamp.h

nodist_libboilerplate_la_SOURCES = config-dump.h
BUILT_SOURCES = config-dump.h
CLEANFILES = config-dump.h

config-dump.h: Makefile $(CONFIG_HEADER)
	@( sed -e '/^#define CONFIG_/!d' \
	-e 's/^#[\t ]*define \(CONFIG_[^ ]*\) \(.*\)/\1=\2/' \
	-e 's,\",\\&,g' \
	-e 's/^.*$$/\"&\",/' $(CONFIG_HEADER); echo '"---",'; \
	sed -e '/#undef CONFIG_/!d' \
	-e 's/.*\(CONFIG_[^ ]*\).*/\1/' \
	-e 's/^.*$$/\"& is OFF\",/' $(CONFIG_HEADER); echo '"---",' ) > $@

libboilerplate_la_CPPFLAGS =				\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/include

libavl_la_SOURCES = avl.c

libavl_la_CPPFLAGS = 			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-include boilerplate/avl.h

if XENO_PSHARED

noinst_LTLIBRARIES += libshavl.la
libboilerplate_la_LIBADD += libshavl.la

libshavl_la_SOURCES = avl.c

libshavl_la_CPPFLAGS = 			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-include boilerplate/shavl.h

endif

libiniparser_la_SOURCES	=	\
	iniparser/dictionary.c	\
	iniparser/dictionary.h	\
	iniparser/iniparser.h	\
	iniparser/iniparser.c

libiniparser_la_CPPFLAGS	=		\
	-Iiniparser @XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include

EXTRA_DIST = iniparser/README iniparser/LICENSE

# Always build TLSF for benchmarking purpose via the
# smokey testsuite.

libboilerplate_la_LIBADD +=	libtlsf.la
noinst_LTLIBRARIES += libtlsf.la

libtlsf_la_SOURCES =	\
	tlsf/tlsf.c	\
	tlsf/tlsf.h	\
	tlsf/target.h

libtlsf_la_CPPFLAGS =					\
	-Itlsf @XENO_USER_CFLAGS@			\
	-I$(top_srcdir)/include				\
	-DTLSF_USE_LOCKS=1 -DUSE_MMAP=1 -DTLSF_STATISTIC=1

SUBDIRS = init

EXTRA_DIST += tlsf/README

SPARSE = sparse

sparse:
	@for i in $(libboilerplate_la_SOURCES) $(libtlsf_la_SOURCES) $(libiniparser_la_SOURCES; do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done

.PHONY: git-stamp
