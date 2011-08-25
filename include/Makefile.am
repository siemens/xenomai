nodist_include_HEADERS=$(CONFIG_HEADER)

if XENO_COBALT

include_HEADERS = \
	rtdk.h

SUBDIRS = \
	asm-generic \
	asm-blackfin \
	asm-x86 \
	asm-powerpc \
	asm-arm \
	asm-nios2 \
	asm-sh \
	asm-sim \
	analogy \
	native \
	psos \
	vxworks \
	cobalt \
	rtdm

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

endif

EXTRA_DIST = testing
