lib_LTLIBRARIES = librtdm.la

librtdm_la_LDFLAGS = -module -version-info 0:0:0

librtdm_la_SOURCES = \
	core.c \
	init.c

librtdm_la_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include

install-exec-hook:
	for la in $(lib_LTLIBRARIES); do \
	    sed -e 's/shouldnotlink=yes/shouldnotlink=no/' $(DESTDIR)$(libdir)/$$la > $(DESTDIR)$(libdir)/$$la.patched; \
	    mv $(DESTDIR)$(libdir)/$$la.patched $(DESTDIR)$(libdir)/$$la; \
	done
