ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai

obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_MITE) += analogy_ni_mite.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_TIO) += analogy_ni_tio.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_MIO) += analogy_ni_mio.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_PCIMIO) += analogy_ni_pcimio.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_670x) += analogy_ni_670x.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_660x) += analogy_ni_660x.o

analogy_ni_mite-y := mite.o
analogy_ni_tio-y := tio_common.o
analogy_ni_mio-y := mio_common.o
analogy_ni_pcimio-y := pcimio.o
analogy_ni_670x-y := ni_670x.o
analogy_ni_660x-y := ni_660x.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_MITE) += analogy_ni_mite.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_TIO) += analogy_ni_tio.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_MIO) += analogy_ni_mio.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_PCIMIO) += analogy_ni_pcimio.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_670x) += analogy_ni_670x.o
obj-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_660x) += analogy_ni_670x.o

analogy_ni_mite-objs := mite.o
analogy_ni_tio-objs := tio_common.o
analogy_ni_mio-objs := mio_common.o
analogy_ni_pcimio-objs := pcimio.o

export-objs := $(analogy_ni_mite-objs) \
	$(analogy_ni_tio-objs) \
	$(analogy_ni_mio-objs) \
	$(analogy_ni_pcimio-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

analogy_ni_mite.o: $(analogy_ni_mite-objs)
	$(LD) -r -o $@ $(analogy_ni_mite-objs)

analogy_ni_tio.o: $(analogy_ni_tio-objs)
	$(LD) -r -o $@ $(analogy_ni_tio-objs)

analogy_ni_mio.o: $(analogy_ni_mio-objs)
	$(LD) -r -o $@ $(analogy_ni_mio-objs)

analogy_ni_pcimio.o: $(analogy_ni_pcimio-objs)
	$(LD) -r -o $@ $(analogy_ni_pcimio-objs)

endif
