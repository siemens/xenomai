
obj-$(CONFIG_XENOMAI) += sh_hal.o

sh_hal-y := machine.o mayday.o thread.o trampoline.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
