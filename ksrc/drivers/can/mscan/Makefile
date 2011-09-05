ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai -Idrivers/xenomai/can -Idrivers/xenomai/can/mscan

obj-$(CONFIG_XENO_DRIVERS_CAN_MSCAN) += xeno_can_mscan.o

xeno_can_mscan-y := rtcan_mscan.o rtcan_mscan_proc.o
ifdef CONFIG_XENO_DRIVERS_CAN_MSCAN_OLD
xeno_can_mscan-y += rtcan_mscan_mpc52xx.o
else
xeno_can_mscan-y += rtcan_mscan_mpc5xxx.o
endif

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_CAN_MSCAN) := xeno_can_mscan.o

list-multi := xeno_can_mscan.o

xeno_can_mscan-objs := rtcan_mscan.o rtcan_mscan_proc.o rtcan_mscan_mpc52xx.o

export-objs := $(xeno_can_mscan-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat -I..

include $(TOPDIR)/Rules.make

xeno_can_mscan.o: $(xeno_can_mscan-objs)
	$(LD) -r -o $@ $(xeno_can_mscan-objs)

endif
