ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENOMAI) += native/ posix/ rtdm/ uvm/ psos+/ vrtx/ vxworks/ uitron/ rtai/

else

# Makefile frag for Linux v2.4

subdir-$(CONFIG_XENOMAI) += native posix rtdm uvm psos+ vrtx vxworks uitron rtai

include $(TOPDIR)/Rules.make

endif
