
ifeq ($(CONFIG_X86_64),y)
X86_MODE := 64
else
X86_MODE := 32
endif

obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal_$(X86_MODE).o hal-common.o usercopy_$(X86_MODE).o

xeno_hal-$(CONFIG_XENO_HW_SMI_DETECT) += smi.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai
