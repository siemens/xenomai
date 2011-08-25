
obj-$(CONFIG_XENO_SKIN_RTDM) += xeno_rtdm.o

xeno_rtdm-y := core.o device.o drvlib.o module.o syscall.o

xeno_rtdm-$(CONFIG_PROC_FS) += proc.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai -I$(src)/..
