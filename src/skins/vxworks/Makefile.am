lib_LTLIBRARIES = libvxworks.la

libvxworks_la_LDFLAGS = -module -version-info 0:0:0 -lpthread

libvxworks_la_SOURCES = \
	errnoLib.c \
	intLib.c \
	msgQLib.c \
	kernelLib.c \
	semLib.c \
	taskInfo.c \
	taskLib.c \
	tickLib.c \
	sysLib.c \
	wdLib.c \
	init.c

libvxworks_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

install-exec-hook:
	for la in $(lib_LTLIBRARIES) ; do \
	    sed -e 's/shouldnotlink=yes/shouldnotlink=no/' $(DESTDIR)$(libdir)/$$la > $(DESTDIR)$(libdir)/$$la.patched ; \
	    mv $(DESTDIR)$(libdir)/$$la.patched $(DESTDIR)$(libdir)/$$la ; \
	done
