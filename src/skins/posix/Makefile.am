includedir = $(prefix)/include/posix
pkgconfigdir = $(libdir)/pkgconfig

if CONFIG_XENO_FAST_WRAP
pkgconfig_DATA = libxenomai_posix.pc
endif

lib_LTLIBRARIES = libpthread_rt.la

CPPFLAGS+=-I$(top_srcdir)/ksrc/skins

libpthread_rt_la_LDFLAGS = @XENO_DLOPEN_CONSTRAINT@ -version-info 1:0:0 -lpthread

libpthread_rt_la_SOURCES = \
	init.c \
	thread.c \
	timer.c \
	semaphore.c \
	clock.c \
	cond.c \
	mq.c \
	mutex.c \
	shm.c \
	interrupt.c \
	select.c \
	rtdm.c \
	printf.c \
	wrappers.c

libpthread_rt_la_CPPFLAGS = \
	@XENO_LIB_CFLAGS@ \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/include/posix

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	$(INSTALL_DATA) $(srcdir)/posix.wrappers $(DESTDIR)$(libdir)

uninstall-local:
	$(RM) $(DESTDIR)$(libdir)/posix.wrappers

EXTRA_DIST = posix.wrappers
