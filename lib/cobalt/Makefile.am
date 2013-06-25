pkgconfigdir = $(libdir)/pkgconfig

SUBDIRS = sysdeps

noinst_HEADERS =	\
	current.h	\
	sem_heap.h	\
	internal.h

lib_LTLIBRARIES = libcobalt.la

libcobalt_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 2:0:0 -lpthread -lrt

libcobalt_la_LIBADD = sysdeps/@XENO_TARGET_ARCH@/libsysdeps.la

libcobalt_la_SOURCES =		\
	assert_context.c	\
	attr.c			\
	clock.c			\
	cond.c			\
	current.c		\
	init.c			\
	internal.c		\
	mq.c			\
	mutex.c			\
	printf.c		\
	rtdm.c			\
	select.c		\
	sem_heap.c		\
	semaphore.c		\
	sigshadow.c		\
	thread.c		\
	timer.c			\
	trace.c			\
	wrappers.c

libcobalt_la_CPPFLAGS =					\
	@XENO_COBALT_CFLAGS@ -Wno-missing-prototypes	\
	-I$(top_srcdir)/kernel				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/include

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	$(INSTALL_DATA) $(srcdir)/cobalt.wrappers $(DESTDIR)$(libdir)

uninstall-local:
	$(RM) $(DESTDIR)$(libdir)/cobalt.wrappers

EXTRA_DIST = cobalt.wrappers
