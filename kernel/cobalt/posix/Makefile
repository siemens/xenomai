obj-$(CONFIG_XENOMAI) += posix.o

posix-y :=		\
	clock.o		\
	cond.o		\
	cond_attr.o	\
	event.o		\
	init.o		\
	monitor.o	\
	mq.o		\
	mutex.o		\
	mutex_attr.o	\
	registry.o	\
	sem.o		\
	syscall.o	\
	thread.o	\
	timer.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
