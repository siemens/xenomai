obj-$(CONFIG_XENOMAI) += nucleus/ rtdm/ xeno_cobalt.o

xeno_cobalt-y := \
	thread_attr.o \
	thread.o \
	mutex_attr.o \
	mutex.o \
	cond_attr.o \
	cond.o \
	sem.o \
	clock.o \
	timer.o \
	registry.o \
	mq.o \
	module.o \
	apc.o \
	syscall.o \
	monitor.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
