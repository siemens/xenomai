
obj-$(CONFIG_XENOMAI) += x86_hal.o
x86_hal-y := machine.o mayday.o thread.o smi.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
