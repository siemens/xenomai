ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

obj-$(CONFIG_XENOMAI) += generic/

else

# Makefile frag for Linux v2.4

subdir-y += generic

include $(TOPDIR)/Rules.make

endif
