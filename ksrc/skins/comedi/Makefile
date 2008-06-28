ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_COMEDI) += xeno_comedi.o

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

xeno_comedi-$(CONFIG_XENO_OPT_PERVASIVE) += interface.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai -I$(src)/..

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_SKIN_COMEDI) := xeno_comedi.o

list-multi := xeno_comedi.o

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

opt_objs-y :=
opt_objs-$(CONFIG_XENO_OPT_PERVASIVE) += interface.o

xeno_comedi-objs += $(opt_objs-y)

export-objs := $(xeno_comedi-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat -I..

include $(TOPDIR)/Rules.make

xeno_comedi.o: $(xeno_comedi-objs)
	$(LD) -r -o $@ $(xeno_comedi-objs)

endif
