ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

obj-$(CONFIG_XENOMAI) += serial/ testing/ can/ analogy/ ipc/

else

# Makefile frag for Linux v2.4

mod-subdirs := serial testing can analogy ipc

subdir-$(CONFIG_XENO_DRIVERS_16550A) += serial

subdir-$(CONFIG_XENO_DRIVERS_TIMERBENCH) += testing
subdir-$(CONFIG_XENO_DRIVERS_IRQBENCH)   += testing
subdir-$(CONFIG_XENO_DRIVERS_SWITCHTEST) += testing

subdir-$(CONFIG_XENO_DRIVERS_CAN) += can
subdir-$(CONFIG_XENO_DRIVERS_ANALOGY) += analogy
subdir-$(CONFIG_XENO_DRIVERS_MPIPE) += ipc

include $(TOPDIR)/Rules.make

endif
