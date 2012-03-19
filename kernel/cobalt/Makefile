obj-$(CONFIG_XENOMAI) += nucleus/ rtdm/ xeno_cobalt.o

xeno_cobalt-y := \
	clock.o \
	cond.o \
	cond_attr.o \
	event.o \
	module.o \
	monitor.o \
	mq.o \
	mutex.o \
	mutex_attr.o \
	registry.o \
	sem.o \
	syscall.o \
	thread.o \
	timer.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
