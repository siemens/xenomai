
lib_LTLIBRARIES = libcopperplate.la

libcopperplate_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0

libcopperplate_la_SOURCES =	\
	clockobj.c	\
	cluster.c	\
	eventobj.c 		\
	hash.c 		\
	init.c		\
	internal.c	\
	internal.h	\
	syncobj.c	\
	semobj.c	\
	threadobj.c	\
	timerobj.c	\
	traceobj.c

libcopperplate_la_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)			\
	-I$(top_srcdir)/include

if XENO_COBALT
libcopperplate_la_CPPFLAGS +=	-I$(top_srcdir)/lib
else
libcopperplate_la_SOURCES += notifier.c
endif

libcopperplate_la_LIBADD = libversion.la

noinst_LTLIBRARIES = libversion.la
libversion_la_CFLAGS = @XENO_USER_CFLAGS@
libversion_la_SOURCES = version.c
version.c: git-stamp.h

git-stamp.h: git-stamp
	@set -x; if test -r $(top_srcdir)/.git; then						\
	  stamp=`git --git-dir=$(top_srcdir)/.git rev-list --abbrev-commit -1 HEAD`;	\
	  if test \! -s $@ || grep -wvq $$stamp $@; then				\
		date=`git --git-dir=$(top_srcdir)/.git log -1 $$stamp --pretty=format:%ci`;	\
		echo "#define GIT_STAMP \"#$$stamp ($$date)\"" > $@;			\
	  fi;										\
	elif test \! -r $@ -o -s $@; then						\
	    rm -f $@ && touch $@;							\
	fi; true

clean-local:
	$(RM) git-stamp.h

if XENO_REGISTRY
libcopperplate_la_LIBADD += libregistry.la
noinst_LTLIBRARIES += libregistry.la

libregistry_la_SOURCES = registry.c

libregistry_la_CPPFLAGS =		\
	$(libcopperplate_la_CPPFLAGS)	\
	@XENO_FUSE_CFLAGS@
endif

if XENO_DEBUG
libcopperplate_la_SOURCES += debug.c
endif

if XENO_PSHARED
# The process shareable heap has real-time properties, therefore it
# fits both the cobalt and mercury cores equally. Yummie.
libcopperplate_la_SOURCES += heapobj-pshared.c reference.c
endif
# We always need to build tlsf/malloc support; in the pshared case, it
# will provide for private memory allocation.
if XENO_TLSF
libcopperplate_la_LIBADD +=	libtlsf.la
noinst_LTLIBRARIES += libtlsf.la
libcopperplate_la_SOURCES += heapobj-tlsf.c
else
libcopperplate_la_SOURCES += heapobj-malloc.c
endif

libtlsf_la_SOURCES =	\
	tlsf/tlsf.c	\
	tlsf/tlsf.h	\
	tlsf/target.h

libtlsf_la_CPPFLAGS =			\
	-Itlsf @XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include		\
	-DTLSF_USE_LOCKS=1 -DUSE_MMAP=1 -DTLSF_STATISTIC=1

EXTRA_DIST = tlsf/README

SPARSE = sparse

sparse:
	@for i in $(libcopperplate_la_SOURCES) $(libtlsf_la_SOURCES); do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done

.PHONY: git-stamp
