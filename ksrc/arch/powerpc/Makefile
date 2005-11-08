
obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o

ifeq ($(CONFIG_PPC64),y)
xeno_hal-$(CONFIG_PPC64) += switch_64.o
xeno_hal-$(CONFIG_XENO_HW_FPU) += fpu_64.o
else
xeno_hal-$(CONFIG_PPC) += switch.o
xeno_hal-$(CONFIG_XENO_HW_FPU) += fpu.o
endif
