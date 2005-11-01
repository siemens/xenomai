includedir = $(prefix)/include/posix

lib_LTLIBRARIES = libpthread_rt.la

libpthread_rt_la_LDFLAGS = -module -version-info 0:0:0

libpthread_rt_la_SOURCES = \
	init.c \
	thread.c \
	timer.c \
	semaphore.c \
	clock.c \
	cond.c \
	mq.c \
	mutex.c \
	interrupt.c \
	rtdm.c \
	wrappers.c

include_HEADERS = \
	mqueue.h \
	pthread.h \
	semaphore.h

libpthread_rt_la_CPPFLAGS = @XENO_USER_CFLAGS@ -I$(srcdir)/..

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	$(INSTALL_DATA) $(srcdir)/posix.wrappers $(DESTDIR)$(libdir)

uninstall-local:
	$(RM) $(DESTDIR)$(libdir)/posix.wrappers

EXTRA_DIST = posix.wrappers
