ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai

obj-$(CONFIG_XENO_DRIVERS_COMEDI_FAKE) += comedi_fake.o
obj-$(CONFIG_XENO_DRIVERS_COMEDI_LOOP) += comedi_loop.o

comedi_fake-y := fake.o

comedi_loop-y := loop.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_COMEDI_FAKE) += comedi_fake.o
obj-$(CONFIG_XENO_DRIVERS_COMEDI_LOOP) += comedi_loop.o

comedi_fake-objs := fake.o

comedi_loop-objs := loop.o

export-objs := $(comedi_fake-objs) $(comedi_loop-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

comedi_fake.o: $(comedi_fake-objs)
	$(LD) -r -o $@ $(comedi_fake-objs)

comedi_loop.o: $(comedi_loop-objs)
	$(LD) -r -o $@ $(comedi_loop-objs)

endif
