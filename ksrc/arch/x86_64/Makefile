
obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o switch.o usercopy.o

xeno_hal-$(CONFIG_XENO_HW_SMI_DETECT) += smi.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai
