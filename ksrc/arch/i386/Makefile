ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o usercopy.o

xeno_hal-$(CONFIG_XENO_HW_NMI_DEBUG_LATENCY) += nmi.o

xeno_hal-$(CONFIG_XENO_HW_SMI_DETECT) += smi.o

else

# Makefile frag for Linux v2.4

USE_STANDARD_AS_RULE := true

O_TARGET := xenomai.o

obj-y := hal.o

obj-$(CONFIG_XENO_HW_NMI_DEBUG_LATENCY) += nmi.o

obj-$(CONFIG_XENO_HW_SMI_DETECT) += smi.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
