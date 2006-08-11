ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

EXTRA_CFLAGS += -Iinclude/xenomai -Idrivers/xenomai/can -Idrivers/xenomai/can/mscan

obj-$(CONFIG_XENO_DRIVERS_RTCAN_MSCAN) += xeno_rtcan_mscan.o

xeno_rtcan_mscan-y := rtcan_mscan.o rtcan_mscan_proc.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_RTCAN_MSCAN) := xeno_rtcan_mscan.o 

list-multi := xeno_rtcan_mscan.o 

xeno_rtcan_mscan-objs := rtcan_mscan.o rtcan_mscan_proc.o

export-objs := $(xeno_rtcan_mscan-objs)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat -I..

include $(TOPDIR)/Rules.make

xeno_rtcan_mscan.o: $(xeno_rtcan_mscan-objs)
	$(LD) -r -o $@ $(xeno_rtcan_mscan-objs)

endif
