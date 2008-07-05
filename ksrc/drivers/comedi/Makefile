ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai -Idrivers/xenomai/comedi

obj-$(CONFIG_XENO_DRIVERS_COMEDI) += xeno_comedi.o testing/

xeno_comedi-y := \
	buffer.o \
	command.o \
	device.o \
	driver.o \
	driver_facilities.o \
	instruction.o \
	os_facilities.o \
	subdevice.o \
	transfer.o

xeno_comedi-$(CONFIG_XENO_OPT_PERVASIVE) += rtdm_interface.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

subdir-$(CONFIG_XENO_DRIVERS_COMEDI_FAKE) += testing
subdir-$(CONFIG_XENO_DRIVERS_COMEDI_LOOP) += testing

obj-$(CONFIG_XENO_DRIVERS_COMEDI) += xeno_comedi.o

opt_objs-y :=
opt_objs-$(CONFIG_XENO_OPT_PERVASIVE) += rtdm_interface.o

xeno_comedi-objs := \
	buffer.o \
	command.o \
	device.o \
	driver.o \
	driver_facilities.o \
	instruction.o \
	os_facilities.o \
	subdevice.o \
	transfer.o

xeno_comedi-objs += $(opt_objs-y)

export-objs := $(xeno_comedi-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat -I.

include $(TOPDIR)/Rules.make

xeno_comedi.o: $(xeno_comedi-objs)
	$(LD) -r -o $@ $(xeno_comedi-objs)

endif
