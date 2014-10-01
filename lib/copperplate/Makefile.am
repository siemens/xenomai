
lib_LTLIBRARIES = libcopperplate.la

libcopperplate_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -lpthread -lrt -version-info 0:0:0

libcopperplate_la_SOURCES =	\
	clockobj.c	\
	cluster.c	\
	eventobj.c 	\
	init.c		\
	internal.c	\
	internal.h	\
	main.c		\
	syncobj.c	\
	semobj.c	\
	threadobj.c	\
	timerobj.c	\
	traceobj.c	\
	wrappers.c

libcopperplate_la_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)			\
	-I$(top_srcdir)/include		\
	-I$(top_srcdir)/lib

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

sbin_PROGRAMS = version
version_CFLAGS = @XENO_USER_CFLAGS@ -I$(top_srcdir)/include -D__PROGRAM__
version_SOURCES = version.c

clean-local:
	$(RM) git-stamp.h

libcopperplate_la_LIBADD = libversion.la

if XENO_MERCURY
libcopperplate_la_LIBADD += ../boilerplate/libboilerplate.la
endif

if XENO_REGISTRY
libcopperplate_la_LIBADD += libregistry.la
noinst_LTLIBRARIES += libregistry.la

libregistry_la_SOURCES = registry.c

libregistry_la_CPPFLAGS =		\
	$(libcopperplate_la_CPPFLAGS)	\
	@XENO_FUSE_CFLAGS@
endif

if XENO_PSHARED
# The process shareable heap has real-time properties, therefore it
# fits both the cobalt and mercury cores equally. Yummie.
libcopperplate_la_SOURCES += heapobj-pshared.c reference.c
endif
if XENO_TLSF
libcopperplate_la_SOURCES += heapobj-tlsf.c
else
libcopperplate_la_SOURCES += heapobj-malloc.c
endif

SUBDIRS = .

if XENO_REGISTRY
SUBDIRS += regd
endif

DIST_SUBDIRS = regd

SPARSE = sparse

sparse:
	@for i in $(libcopperplate_la_SOURCES) ; do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done

.PHONY: git-stamp
