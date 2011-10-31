
obj-y += powerpc_hal.o

powerpc_hal-y := hal.o switch_$(CONFIG_WORD_SIZE).o

powerpc_hal-$(CONFIG_XENO_HW_FPU) += fpu.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
