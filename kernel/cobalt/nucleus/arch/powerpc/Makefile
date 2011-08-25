
obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o switch_$(CONFIG_WORD_SIZE).o

xeno_hal-$(CONFIG_XENO_HW_FPU) += fpu.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
