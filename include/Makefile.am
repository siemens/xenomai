
SUBDIRS = \
	asm-generic \
	asm-bfinnommu \
	asm-i386 \
	asm-ia64 \
	asm-powerpc \
	asm-uvm \
	asm-sim \
	native \
	nucleus \
	posix \
	psos+ \
	rtai \
	rtdm \
	uitron \
	uvm \
	vrtx \
	vxworks

install-data-local:
	rm -f $(DESTDIR)$(includedir)/asm
	$(LN_S) asm-@XENO_TARGET_ARCH@ $(DESTDIR)$(includedir)/asm
	rm -f $(DESTDIR)$(includedir)/xenomai
	$(LN_S) . $(DESTDIR)$(includedir)/xenomai
	rm -f $(DESTDIR)$(includedir)/asm-@XENO_TARGET_ARCH@/xenomai
	$(LN_S) . $(DESTDIR)$(includedir)/asm-@XENO_TARGET_ARCH@/xenomai
	rm -f $(DESTDIR)$(includedir)/asm-generic/xenomai
	$(LN_S) . $(DESTDIR)$(includedir)/asm-generic/xenomai

uninstall-local:
	rm -f $(DESTDIR)$(includedir)/asm
	rm -f $(DESTDIR)$(includedir)/xenomai
	rm -f $(DESTDIR)$(includedir)/asm-@XENO_TARGET_ARCH@/xenomai
	rm -f $(DESTDIR)$(includedir)/asm-generic/xenomai
