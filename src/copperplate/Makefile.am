
lib_LTLIBRARIES = libcopperplate.la

libcopperplate_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 0:0:0 -lpthread

libcopperplate_la_SOURCES =	\
	clockobj.c	\
	hash.c 		\
	init.c		\
	panic.c		\
	syncobj.c	\
	timerobj.c	\
	traceobj.c

libcopperplate_la_CPPFLAGS =		\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/src/include

libcopperplate_la_LIBADD =

if XENO_COBALT
libcopperplate_la_CPPFLAGS +=	\
	-I$(top_srcdir)/include/posix	\
	-I$(top_srcdir)/include
libcopperplate_la_SOURCES +=	\
	threadobj-cobalt.c
else
libcopperplate_la_SOURCES +=	\
	threadobj-mercury.c \
	notifier.c
endif

if XENO_REGISTRY
libcopperplate_la_LIBADD += libregistry.la
noinst_LTLIBRARIES = libregistry.la

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
# We always need to build tlsf/malloc support; in the pshared case, it
# will provide for private memory allocation.
if XENO_TLSF
libcopperplate_la_LIBADD +=	libtlsf.la
noinst_LTLIBRARIES = libtlsf.la
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
	-DTLSF_USE_LOCKS=1 -DUSE_MMAP=1 -DUSE_SBRK=1

EXTRA_DIST = tlsf/README

SPARSE = sparse

sparse:
	@for i in $(libcopperplate_la_SOURCES) $(libtlsf_la_SOURCES); do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done
