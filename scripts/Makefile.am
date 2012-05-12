bin_SCRIPTS=xeno-config xeno wrap-link.sh

install-exec-local:
	@if test -r $(srcdir)/postinstall.sh ; then \
	   echo "Running post-install script..." ;\
	   $(srcdir)/postinstall.sh ; \
	fi

EXTRA_DIST = \
	bootstrap \
	prepare-kernel.sh \
	prepare-patch.sh \
	wrap-link.sh \
	Kconfig.frag \
	Modules.frag \
	defconfig.frag \
	help_from_kconfig.pl \
	$(wildcard postinstall.sh) \
	histo.gp
