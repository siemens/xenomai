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
	nsem.o		\
	sem.o		\
	select.o	\
	signal.o	\
	syscall.o	\
	thread.o	\
	timer.o		\
	timerfd.o

ccflags-y := -Iarch/$(SRCARCH)/xenomai/include -Iinclude/xenomai -Ikernel
