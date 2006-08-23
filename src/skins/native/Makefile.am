lib_LTLIBRARIES = libnative.la

libnative_la_LDFLAGS = -module -version-info 0:0:0 -lpthread

libnative_la_SOURCES = \
	alarm.c \
	cond.c \
	event.c \
	heap.c \
	init.c \
	intr.c \
	misc.c \
	mutex.c \
	pipe.c \
	queue.c \
	sem.c \
	task.c \
	timer.c

libnative_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

install-exec-hook:
	for la in $(lib_LTLIBRARIES) ; do \
	    sed -e 's/shouldnotlink=yes/shouldnotlink=no/' $(DESTDIR)$(libdir)/$$la > $(DESTDIR)$(libdir)/$$la.patched ; \
	    mv $(DESTDIR)$(libdir)/$$la.patched $(DESTDIR)$(libdir)/$$la ; \
	done
