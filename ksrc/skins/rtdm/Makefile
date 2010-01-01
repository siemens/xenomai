
obj-$(CONFIG_XENO_SKIN_RTDM) += xeno_rtdm.o

xeno_rtdm-y := core.o device.o drvlib.o module.o

xeno_rtdm-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

xeno_rtdm-$(CONFIG_PROC_FS) += proc.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai -I$(src)/..
