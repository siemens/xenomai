
obj-$(CONFIG_XENO_SKIN_NATIVE) += xeno_native.o

xeno_native-y := module.o task.o timer.o

xeno_native-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_REGISTRY) += registry.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_PIPE) += pipe.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_SEM) += sem.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_EVENT) += event.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_MUTEX) += mutex.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_COND) += cond.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_QUEUE) += queue.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_HEAP) += heap.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_ALARM) += alarm.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_INTR) += intr.o

extra-clean := $(obj-) $(xeno_native-) $(xeno_native-y) xeno_native.mod.o built-in.o

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.d)		\
		$(wildcard $(obj)/.*.cmd)	\
		$(wildcard $(obj)/*.mod.c)	\
		$(wildcard $(obj)/*.ko))
