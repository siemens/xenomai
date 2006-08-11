ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENOMAI) += 16550A/ testing/ can/

else

# Makefile frag for Linux v2.4

mod-subdirs := 16550A testing can

subdir-$(CONFIG_XENO_DRIVERS_16550A) += 16550A

subdir-$(CONFIG_XENO_DRIVERS_TIMERBENCH) += testing
subdir-$(CONFIG_XENO_DRIVERS_IRQBENCH)   += testing
subdir-$(CONFIG_XENO_DRIVERS_SWITCHTEST) += testing

subdir-$(CONFIG_XENO_DRIVERS_RTCAN) += can

include $(TOPDIR)/Rules.make

endif
