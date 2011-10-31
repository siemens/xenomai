obj-$(CONFIG_XENOMAI) += arm_hal.o

arm_hal-y := hal.o switch.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
