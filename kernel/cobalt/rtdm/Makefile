
obj-$(CONFIG_XENOMAI) += rtdm.o

rtdm-y :=	core.o		\
		device.o	\
		drvlib.o	\
		init.o		\
		syscall.o

rtdm-$(CONFIG_PROC_FS) += proc.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai -I$(src)/..
