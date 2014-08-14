
obj-$(CONFIG_XENOMAI) += xenomai.o

xenomai-y := machine.o mayday.o thread.o switch_$(CONFIG_WORD_SIZE).o

xenomai-$(CONFIG_XENO_HW_FPU) += fpu.o

ccflags-y := -Iarch/powerpc/xenomai/include -Iinclude/xenomai
