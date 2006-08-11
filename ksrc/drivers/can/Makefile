ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

EXTRA_CFLAGS += -Iinclude/xenomai -Idrivers/xenomai/can

obj-$(CONFIG_XENO_DRIVERS_RTCAN) += xeno_rtcan.o mscan/ sja1000/

xeno_rtcan-y := rtcan_dev.o rtcan_socket.o rtcan_module.o rtcan_raw.o rtcan_raw_dev.o rtcan_raw_filter.o

else

# Makefile frag for Linux v2.4

mod-subdirs := mscan sja1000

subdir-$(CONFIG_XENO_DRIVERS_RTCAN_MSCAN) += mscan
subdir-$(CONFIG_XENO_DRIVERS_RTCAN_SJA1000) += sja1000

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_RTCAN) := xeno_rtcan.o

list-multi := xeno_rtcan.o

xeno_rtcan-objs := rtcan_dev.o rtcan_socket.o rtcan_module.o rtcan_raw.o rtcan_raw_dev.o rtcan_raw_filter.o

export-objs := $(xeno_rtcan-objs)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat -I.

include $(TOPDIR)/Rules.make

xeno_rtcan.o: $(xeno_rtcan-objs)
	$(LD) -r -o $@ $(xeno_rtcan-objs)

endif
