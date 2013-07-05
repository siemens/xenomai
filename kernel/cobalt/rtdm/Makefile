
obj-$(CONFIG_XENOMAI) += rtdm.o

rtdm-y :=	core.o		\
		device.o	\
		drvlib.o	\
		init.o		\
		syscall.o

rtdm-$(CONFIG_PROC_FS) += proc.o

ccflags-y := -Iarch/$(SRCARCH)/xenomai/include -Iinclude/xenomai -I$(src)/..
