ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENOMAI) += serial/ testing/ can/ comedi/

else

# Makefile frag for Linux v2.4

mod-subdirs := serial testing can comedi

subdir-$(CONFIG_XENO_DRIVERS_16550A) += serial

subdir-$(CONFIG_XENO_DRIVERS_TIMERBENCH) += testing
subdir-$(CONFIG_XENO_DRIVERS_IRQBENCH)   += testing
subdir-$(CONFIG_XENO_DRIVERS_SWITCHTEST) += testing

subdir-$(CONFIG_XENO_DRIVERS_CAN) += can
subdir-$(CONFIG_XENO_DRIVERS_COMEDI) += comedi

include $(TOPDIR)/Rules.make

endif
