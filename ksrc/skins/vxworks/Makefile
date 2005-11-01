
obj-$(CONFIG_XENO_SKIN_VXWORKS) += xeno_vxworks.o

xeno_vxworks-y := errnoLib.o intLib.o kernelLib.o module.o msgQLib.o semLib.o \
		sysLib.o taskHookLib.o taskInfo.o taskLib.o tickLib.o wdLib.o

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.d)		\
		$(wildcard $(obj)/.*.cmd)	\
		$(wildcard $(obj)/*.mod.c)	\
		$(wildcard $(obj)/*.ko))
