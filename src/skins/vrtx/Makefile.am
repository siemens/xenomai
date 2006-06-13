lib_LTLIBRARIES = libvrtx.la

libvrtx_la_LDFLAGS = -module -version-info 0:0:0 -lpthread

libvrtx_la_SOURCES = \
	event.c \
	init.c \
	mb.c \
	mx.c \
	queue.c \
	sem.c \
	task.c \
	time.c

libvrtx_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

install-exec-hook:
	for la in $(lib_LTLIBRARIES); do \
	    sed -e 's/shouldnotlink=yes/shouldnotlink=no/' $(DESTDIR)$(libdir)/$$la > $(DESTDIR)$(libdir)/$$la.patched; \
	    mv $(DESTDIR)$(libdir)/$$la.patched $(DESTDIR)$(libdir)/$$la; \
	done
