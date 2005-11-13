ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_DRIVERS_16550A) += xeno_16550A.o

xeno_16550A-y := 16550A.o

else

# Makefile frag for Linux v2.4

O_TARGET := xeno_16550A.o

obj-$(CONFIG_XENO_DRIVERS_16550A) := 16550A.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
