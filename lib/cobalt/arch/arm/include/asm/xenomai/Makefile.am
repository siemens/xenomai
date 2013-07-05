includesubdir = $(includedir)/cobalt/asm/xenomai

includesub_HEADERS =	\
	tsc.h

noinst_HEADERS =	\
	features.h	\
	syscall.h

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(includesubdir)/uapi
	$(INSTALL_HEADER) $(top_srcdir)/kernel/cobalt/arch/arm/include/asm/xenomai/uapi/tsc.h $(DESTDIR)$(includesubdir)/uapi/tsc.h
