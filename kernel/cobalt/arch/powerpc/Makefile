
obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o
xeno_hal-$(CONFIG_XENO_HW_FPU) += fpu.o

ifeq ($(CONFIG_PPC64),y)
xeno_hal-$(CONFIG_PPC64) += switch_64.o
else
xeno_hal-$(CONFIG_PPC) += switch_32.o
endif

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai
