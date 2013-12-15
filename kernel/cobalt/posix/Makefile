obj-$(CONFIG_XENOMAI) += posix.o

posix-y :=		\
	clock.o		\
	cond.o		\
	cond_attr.o	\
	event.o		\
	init.o		\
	monitor.o	\
	mqueue.o	\
	mutex.o		\
	mutex_attr.o	\
	registry.o	\
	sem.o		\
	nsem.o		\
	select.o	\
	signal.o	\
	syscall.o	\
	thread.o	\
	timer.o

ccflags-y := -Iarch/$(SRCARCH)/xenomai/include -Iinclude/xenomai
