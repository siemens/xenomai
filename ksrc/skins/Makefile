ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

obj-$(CONFIG_XENOMAI) += native/ posix/ rtdm/

else

# Makefile frag for Linux v2.4

subdir-$(CONFIG_XENO_SKIN_NATIVE) += native
subdir-$(CONFIG_XENO_SKIN_POSIX) += posix
subdir-$(CONFIG_XENO_SKIN_RTDM) += rtdm

include $(TOPDIR)/Rules.make

endif
