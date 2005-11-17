ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o

else

# Makefile frag for Linux v2.4

O_TARGET := xenomai.o

obj-y := hal.o compat.o

export-objs := $(obj-y)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
