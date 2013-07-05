
obj-y += powerpc_hal.o

powerpc_hal-y := machine.o mayday.o thread.o switch_$(CONFIG_WORD_SIZE).o

powerpc_hal-$(CONFIG_XENO_HW_FPU) += fpu.o

ccflags-y := -Iarch/powerpc/xenomai/include -Iinclude/xenomai
