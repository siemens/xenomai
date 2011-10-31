
ifeq ($(CONFIG_X86_64),y)
X86_MODE := 64
else
X86_MODE := 32
endif

obj-$(CONFIG_XENOMAI) += x86_hal.o
x86_hal-y := hal_$(X86_MODE).o hal-common.o usercopy_$(X86_MODE).o
x86_hal-$(CONFIG_XENO_HW_SMI_DETECT) += smi.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
