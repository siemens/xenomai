ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_RTDM) += xeno_rtdm.o

xeno_rtdm-y := core.o device.o drvlib.o module.o proc.o

xeno_rtdm-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

else

# Makefile frag for Linux v2.4

O_TARGET := xeno_rtdm.o

obj-y := core.o device.o drvlib.o module.o proc.o

obj-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
