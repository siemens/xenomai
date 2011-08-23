includedir = $(prefix)/include/posix
pkgconfigdir = $(libdir)/pkgconfig

noinst_HEADERS = \
	sem_heap.h

lib_LTLIBRARIES = libpthread_rt.la

CPPFLAGS += -I$(top_srcdir)/ksrc

libpthread_rt_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 2:0:0 -lpthread -lrt

libpthread_rt_la_SOURCES = \
	assert_context.c \
	bind.c \
	current.c \
	sem_heap.c \
	sigshadow.c \
	timeconv.c \
	init.c \
	thread.c \
	timer.c \
	semaphore.c \
	clock.c \
	cond.c \
	mq.c \
	mutex.c \
	shm.c \
	select.c \
	rtdm.c \
	printf.c \
	wrappers.c

libpthread_rt_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ -Wno-missing-prototypes \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/include/posix

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	$(INSTALL_DATA) $(srcdir)/posix.wrappers $(DESTDIR)$(libdir)

uninstall-local:
	$(RM) $(DESTDIR)$(libdir)/posix.wrappers

EXTRA_DIST = posix.wrappers
