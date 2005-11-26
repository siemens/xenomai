install-exec-local:
	$(mkinstalldirs) $(DESTDIR)$(bindir)
	$(INSTALL_SCRIPT) xeno-config $(DESTDIR)$(bindir)
	$(INSTALL_SCRIPT) xeno-load $(DESTDIR)$(bindir)
	$(INSTALL_SCRIPT) xeno-test $(DESTDIR)$(bindir)
	$(INSTALL_SCRIPT) $(srcdir)/xeno-info $(DESTDIR)$(bindir)
	@if test -r $(srcdir)/postinstall.sh; then \
	   echo "Running post-install script..." ;\
	   $(srcdir)/postinstall.sh ; \
	fi

uninstall-local:
	$(RM) $(DESTDIR)$(bindir)/xeno-info
	$(RM) $(DESTDIR)$(bindir)/xeno-load
	$(RM) $(DESTDIR)$(bindir)/xeno-config
	$(RM) $(DESTDIR)$(bindir)/xeno-test

EXTRA_DIST = xeno-info bootstrap prepare-kernel.sh \
	Kconfig.frag Modules.frag defconfig.frag \
	$(wildcard postinstall.sh)
