ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_VXWORKS) += xeno_vxworks.o

xeno_vxworks-y := errnoLib.o intLib.o kernelLib.o module.o msgQLib.o semLib.o \
		sysLib.o taskHookLib.o taskInfo.o taskLib.o tickLib.o wdLib.o
else

# Makefile frag for Linux v2.4

O_TARGET := xeno_vxworks.o

obj-$(CONFIG_XENO_SKIN_VXWORKS) :=  errnoLib.o intLib.o kernelLib.o module.o msgQLib.o semLib.o \
	sysLib.o taskHookLib.o taskInfo.o taskLib.o tickLib.o wdLib.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
