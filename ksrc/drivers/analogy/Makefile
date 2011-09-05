ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai -Idrivers/xenomai/analogy

obj-$(CONFIG_XENO_DRIVERS_ANALOGY) += xeno_analogy.o testing/ intel/ national_instruments/ sensoray/

xeno_analogy-y := \
	buffer.o \
	command.o \
	device.o \
	driver.o \
	driver_facilities.o \
	instruction.o \
	os_facilities.o \
	subdevice.o \
	transfer.o

xeno_analogy-$(CONFIG_XENO_OPT_PERVASIVE) += rtdm_interface.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

subdir-$(CONFIG_XENO_DRIVERS_ANALOGY_FAKE) += testing
subdir-$(CONFIG_XENO_DRIVERS_ANALOGY_LOOP) += testing
subdir-$(CONFIG_XENO_DRIVERS_ANALOGY_8255) += intel
subdir-$(CONFIG_XENO_DRIVERS_ANALOGY_NI_MITE) += national_instruments
subdir-$(CONFIG_XENO_DRIVERS_ANALOGY_S526) += sensoray

obj-$(CONFIG_XENO_DRIVERS_ANALOGY) += xeno_analogy.o

opt_objs-y :=
opt_objs-$(CONFIG_XENO_OPT_PERVASIVE) += rtdm_interface.o

xeno_analogy-objs := \
	buffer.o \
	command.o \
	device.o \
	driver.o \
	driver_facilities.o \
	instruction.o \
	os_facilities.o \
	subdevice.o \
	transfer.o

xeno_analogy-objs += $(opt_objs-y)

export-objs := $(xeno_analogy-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat -I.

include $(TOPDIR)/Rules.make

xeno_analogy.o: $(xeno_analogy-objs)
	$(LD) -r -o $@ $(xeno_analogy-objs)

endif
