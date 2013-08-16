bin_SCRIPTS=xeno-config xeno wrap-link.sh

install-exec-local:
	@if test -r $(srcdir)/postinstall.sh ; then \
	   echo "Running post-install script..." ;\
	   $(srcdir)/postinstall.sh ; \
	fi

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	$(INSTALL_DATA) $(srcdir)/dynlist.ld $(DESTDIR)$(libdir)

EXTRA_DIST =				\
	bootstrap			\
	prepare-kernel.sh		\
	wrap-link.sh			\
	dynlist.ld			\
	Kconfig.frag			\
	$(wildcard postinstall.sh)	\
	histo.gp
