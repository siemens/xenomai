includedir = $(prefix)/include/posix

lib_LTLIBRARIES = libpthread_rt.la

libpthread_rt_la_LDFLAGS = -module -version-info 0:0:0 -lpthread

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
	rtdm.c \
	wrappers.c

libpthread_rt_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/include/posix

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	$(INSTALL_DATA) $(srcdir)/posix.wrappers $(DESTDIR)$(libdir)

uninstall-local:
	$(RM) $(DESTDIR)$(libdir)/posix.wrappers

install-exec-hook:
	for la in $(lib_LTLIBRARIES) ; do \
	    sed -e 's/shouldnotlink=yes/shouldnotlink=no/' $(DESTDIR)$(libdir)/$$la > $(DESTDIR)$(libdir)/$$la.patched ; \
	    mv $(DESTDIR)$(libdir)/$$la.patched $(DESTDIR)$(libdir)/$$la ; \
	done

EXTRA_DIST = posix.wrappers
