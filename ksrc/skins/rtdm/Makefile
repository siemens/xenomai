
obj-$(CONFIG_XENO_SKIN_RTDM) += xeno_rtdm.o

xeno_rtdm-y := core.o device.o drvlib.o module.o proc.o

xeno_rtdm-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/.*.d)		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.cmd)	\
		$(wildcard $(obj)/*.mod.c)	\
		$(wildcard $(obj)/*.ko))
