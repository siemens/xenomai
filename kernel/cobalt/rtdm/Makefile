
obj-$(CONFIG_XENOMAI) += xenomai.o

xenomai-y :=	core.o		\
		device.o	\
		drvlib.o	\
		fd.o		\
		init.o

xenomai-$(CONFIG_PROC_FS) += proc.o

ccflags-y := -Iarch/$(SRCARCH)/xenomai/include -Iinclude/xenomai -I$(src)/..
