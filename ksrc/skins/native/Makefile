
obj-$(CONFIG_XENO_SKIN_NATIVE) += xeno_native.o

xeno_native-y := module.o task.o timer.o syscall.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_SEM) += sem.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_EVENT) += event.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_MUTEX) += mutex.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_COND) += cond.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_QUEUE) += queue.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_HEAP) += heap.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_ALARM) += alarm.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_INTR) += intr.o

xeno_native-$(CONFIG_XENO_OPT_NATIVE_BUFFER) += buffer.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai
