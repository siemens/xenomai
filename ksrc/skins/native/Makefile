ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

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

else

# Makefile frag for Linux v2.4

O_TARGET := xeno_native.o

obj-y := module.o task.o timer.o

obj-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

obj-$(CONFIG_XENO_OPT_NATIVE_REGISTRY) += registry.o

obj-$(CONFIG_XENO_OPT_NATIVE_PIPE) += pipe.o

obj-$(CONFIG_XENO_OPT_NATIVE_SEM) += sem.o

obj-$(CONFIG_XENO_OPT_NATIVE_EVENT) += event.o

obj-$(CONFIG_XENO_OPT_NATIVE_MUTEX) += mutex.o

obj-$(CONFIG_XENO_OPT_NATIVE_COND) += cond.o

obj-$(CONFIG_XENO_OPT_NATIVE_QUEUE) += queue.o

obj-$(CONFIG_XENO_OPT_NATIVE_HEAP) += heap.o

obj-$(CONFIG_XENO_OPT_NATIVE_ALARM) += alarm.o

obj-$(CONFIG_XENO_OPT_NATIVE_INTR) += intr.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
