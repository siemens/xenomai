KCONFIG_DIR=./config/kconfig
CC := gcc

ifneq ($(MAKECMDGOALS),help)
ifeq ($(xeno_srctree),)
ifneq ($(MAKEFILE_LIST),)
# Since 3.80, we can find out which Makefile is currently processed,
# and infere the location of the source tree using MAKEFILE_LIST.
xeno_srctree := $(dir $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST)))
else
ifeq ($(xeno_srctree),)
xeno_srctree := $(shell test -d config && pwd)
ifeq ($(xeno_srctree),)
$(error Please specify the location of your source tree: make xeno_srctree=...)
endif
endif
endif
endif
build_alias := $(shell $(xeno_srctree)/config/autoconf/config.guess)
host_alias := $(shell  $(xeno_srctree)/config/autoconf/arch2host.sh $(ARCH) $(build_alias))
override ARCH := $(shell echo $(host_alias)|cut -f1 -d-|sed -e s/^i.86/i386/ -e s/^powerpc/ppc/)
endif

ifeq ($(MAKECMDGOALS),)
confgui = $(shell test $(TERM) = emacs && echo xconfig || echo menuconfig)
forceconf = $(shell test -r .xeno_config -a -r .cfok || echo $(confgui))
endif

override xeno_srctree := $(shell cd $(xeno_srctree) && pwd)

all:: $(forceconf)
	@/bin/true

xconfig: qconf config-script

gconfig: gconf config-script

mconfig menuconfig: mconf config-script

config: conf config-script

oldconfig: oldconf config-script

config-script: config.status

reconfig::
	@touch .xeno_config

reconfig:: config-script

config.status: .xeno_config
	@test -r config.status && recf=yes || recf=no ; \
	eval `grep ^CONFIG_XENO_INSTALLDIR $<`; \
	eval `grep ^CONFIG_XENO_MAINT $<`; \
	test x$$CONFIG_XENO_MAINT_AUTOTOOLS = xy && confopts=--enable-maintainer-mode; \
	$(xeno_srctree)/configure \
	CROSS_COMPILE=$(CROSS_COMPILE) \
	CC=$(CROSS_COMPILE)$(CC) \
	CXX=$(CROSS_COMPILE)$(CXX) \
	LD=$(CROSS_COMPILE)$(LD) \
	AR=$(CROSS_COMPILE)$(AR) \
	RANLIB=$(CROSS_COMPILE)ranlib \
	STRIP=$(CROSS_COMPILE)strip \
	NM=$(CROSS_COMPILE)nm \
	--build=$(build_alias) \
	--host=$(host_alias) \
	--with-kconfig-file=$< \
	--with-linux-dir=$(XENO_LINUX_DIR) \
	--prefix=$$CONFIG_XENO_INSTALLDIR \
	$$confopts ; \
	if test $$? = 0; then \
	   touch .cfok ; \
	   if test x$$recf = xyes ; then \
	      touch .cfchanged ; \
	   fi ; \
	else \
	   rm -f .cfok ; false; \
	fi

qconf: $(KCONFIG_DIR)
	@$(MAKE) -C $(KCONFIG_DIR) \
	-f $(xeno_srctree)/$(KCONFIG_DIR)/Makefile.kconfig xconfig \
	srctree=$(xeno_srctree) ARCH=$(ARCH)

gconf: $(KCONFIG_DIR)
	@$(MAKE) -C $(KCONFIG_DIR) \
	-f $(xeno_srctree)/$(KCONFIG_DIR)/Makefile.kconfig gconfig \
	srctree=$(xeno_srctree) ARCH=$(ARCH)

mconf: $(KCONFIG_DIR)
	@$(MAKE) -C $(KCONFIG_DIR) \
	-f $(xeno_srctree)/$(KCONFIG_DIR)/Makefile.kconfig menuconfig \
	srctree=$(xeno_srctree) ARCH=$(ARCH)

conf: $(KCONFIG_DIR)
	@$(MAKE) -C $(KCONFIG_DIR) \
	-f $(xeno_srctree)/$(KCONFIG_DIR)/Makefile.kconfig config \
	srctree=$(xeno_srctree) ARCH=$(ARCH)

oldconf: $(KCONFIG_DIR)
	@$(MAKE) -C $(KCONFIG_DIR) \
	-f $(xeno_srctree)/$(KCONFIG_DIR)/Makefile.kconfig oldconfig \
	srctree=$(xeno_srctree) ARCH=$(ARCH)

$(KCONFIG_DIR):
	@test -d $@ || mkdir -p $@

help:
	@echo ; echo "This is Xenomai's build bootstrapping Makefile. In order to build Xenomai," ; \
	echo 'you first need to configure it. Proceed as follows:' ; \
	echo ; echo '$$ cd $$your_builddir' ; echo ; \
	echo '# Configuration using a KDE-based, GTK-based or Dialog-based GUI' ; \
	echo '$$ make -f $$xeno_srcdir/makefile xeno_srctree=$$xeno_srcdir {xconfig,gconfig,menuconfig}' ; \
	echo '                            OR,' ; \
	echo '# Configuration using a text-based interface' ; \
	echo '$$ make -f $$xeno_srcdir/makefile xeno_srctree=$$xeno_srcdir {config,oldconfig}' ; \
	echo ; echo 'In case a configuration error occurs, re-run the command above to fix the' ; \
	echo 'faulty parameter. Once the configuration is successful, type:' ; echo ; \
	echo '$$ make [all]' ; \
	echo ; echo "To change the configuration from now on, simply run:"; echo ; \
	echo '$$ make {xconfig,gconfig,menuconfig,config}' ; echo

clean distclean:
	if test -r $(KCONFIG_DIR)/Makefile.kconfig ; then \
	$(MAKE) -C $(KCONFIG_DIR) -f Makefile.kconfig clean ; fi
	if test -r GNUmakefile ; then \
	$(MAKE) -f GNUmakefile $@ ; else \
	$(MAKE) -C $(KCONFIG_DIR) -f $(xeno_srctree)/$(KCONFIG_DIR)/Makefile.kconfig clean ; \
	fi
	@find . -name autom4te.cache | xargs rm -fr

all %::
	@if test -r GNUmakefile ; then \
	$(MAKE) -f GNUmakefile $@ ; else \
	echo "*** Please configure Xenomai first (running 'make help' in Xenomai's toplevel dir might help)." ; \
	exit 1; fi

.PHONY: config-script reconfig xconfig gconfig mconfig menuconfig config oldconfig qconf mconf conf clean distclean help
