includedir = $(prefix)/include/asm-sim

include_HEADERS = system.h

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(includedir)
	rm -f $(DESTDIR)$(includedir)/asm
	$(LN_S) . $(DESTDIR)$(includedir)/asm
	rm -f $(DESTDIR)$(includedir)/xenomai
	$(LN_S) . $(DESTDIR)$(includedir)/xenomai

uninstall-local:
	rm -f $(DESTDIR)$(includedir)/asm
	rm -f $(DESTDIR)$(includedir)/xenomai

SUBDIRS = bits
