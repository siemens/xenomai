ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai

obj-$(CONFIG_XENO_DRIVERS_COMEDI_NI_MITE) += comedi_ni_mite.o
obj-$(CONFIG_XENO_DRIVERS_COMEDI_NI_TIO) += comedi_ni_tio.o
obj-$(CONFIG_XENO_DRIVERS_COMEDI_NI_MIO) += comedi_ni_mio.o
obj-$(CONFIG_XENO_DRIVERS_COMEDI_NI_PCIMIO) += comedi_ni_pcimio.o

comedi_ni_mite-y := mite.o
comedi_ni_tio-y := tio_common.o
comedi_ni_mio-y := mio_common.o
comedi_ni_pcimio-y := pcimio.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_COMEDI_NI_MITE) += comedi_ni_mite.o
obj-$(CONFIG_XENO_DRIVERS_COMEDI_NI_TIO) += comedi_ni_tio.o
obj-$(CONFIG_XENO_DRIVERS_COMEDI_NI_MIO) += comedi_ni_mio.o
obj-$(CONFIG_XENO_DRIVERS_COMEDI_NI_PCIMIO) += comedi_ni_pcimio.o

comedi_ni_mite-objs := mite.o
comedi_ni_tio-objs := tio_common.o
comedi_ni_mio-objs := mio_common.o
comedi_ni_pcimio-objs := pcimio.o

export-objs := $(comedi_ni_mite-objs) \
	$(comedi_ni_tio-objs) \
	$(comedi_ni_mio-objs) \
	$(comedi_ni_pcimio-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

comedi_ni_mite.o: $(comedi_ni_mite-objs)
	$(LD) -r -o $@ $(comedi_ni_mite-objs)

comedi_ni_tio.o: $(comedi_ni_tio-objs)
	$(LD) -r -o $@ $(comedi_ni_tio-objs)

comedi_ni_mio.o: $(comedi_ni_mio-objs)
	$(LD) -r -o $@ $(comedi_ni_mio-objs)

comedi_ni_pcimio.o: $(comedi_ni_pcimio-objs)
	$(LD) -r -o $@ $(comedi_ni_pcimio-objs)

endif
