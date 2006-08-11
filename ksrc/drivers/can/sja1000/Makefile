ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

EXTRA_CFLAGS += -Iinclude/xenomai -Idrivers/xenomai/can -Idrivers/xenomai/can/sja1000

obj-$(CONFIG_XENO_DRIVERS_RTCAN_SJA1000) += xeno_rtcan_sja1000.o
obj-$(CONFIG_XENO_DRIVERS_RTCAN_SJA1000_PEAK_PCI) += xeno_rtcan_peak_pci.o
obj-$(CONFIG_XENO_DRIVERS_RTCAN_SJA1000_PEAK_DNG) += xeno_rtcan_peak_dng.o
obj-$(CONFIG_XENO_DRIVERS_RTCAN_SJA1000_ISA) += xeno_rtcan_isa.o

xeno_rtcan_sja1000-y := rtcan_sja1000.o rtcan_sja1000_proc.o
xeno_rtcan_peak_pci-y := rtcan_peak_pci.o
xeno_rtcan_peak_dng-y := rtcan_peak_dng.o
xeno_rtcan_isa-y := rtcan_isa.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_RTCAN_SJA1000) += xeno_rtcan_sja1000.o 
obj-$(CONFIG_XENO_DRIVERS_RTCAN_SJA1000_PEAK_PCI) += xeno_rtcan_peak_pci.o 
obj-$(CONFIG_XENO_DRIVERS_RTCAN_SJA1000_PEAK_DNG) += xeno_rtcan_peak_dng.o 
obj-$(CONFIG_XENO_DRIVERS_RTCAN_SJA1000_ISA) += xeno_rtcan_isa.o 

list-multi := xeno_rtcan_sja1000.o 

xeno_rtcan_sja1000-objs := rtcan_sja1000.o rtcan_sja1000_proc.o
xeno_rtcan_peak_pci-objs := rtcan_peak_pci.o
xeno_rtcan_peak_dng-objs := rtcan_peak_dng.o
xeno_rtcan_isa-objs := rtcan_isa.o

export-objs := $(xeno_rtcan_sja1000-objs)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat -I..  -I.

include $(TOPDIR)/Rules.make

xeno_rtcan_sja1000.o: $(xeno_rtcan_sja1000-objs)
	$(LD) -r -o $@ $(xeno_rtcan_sja1000-objs)

xeno_rtcan_peak_pci.o: $(xeno_rtcan_peak_pci-objs)
	$(LD) -r -o $@ $(xeno_rtcan_peak_pci-objs)

xeno_rtcan_peak_dng.o: $(xeno_rtcan_peak_dng-objs)
	$(LD) -r -o $@ $(xeno_rtcan_peak_dng-objs)

xeno_rtcan_isa.o: $(xeno_rtcan_isa-objs)
	$(LD) -r -o $@ $(xeno_rtcan_isa-objs)
endif
