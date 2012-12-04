obj-$(CONFIG_XENOMAI) += arm_hal.o

arm_hal-y := machine.o mayday.o thread.o switch.o syscall.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
