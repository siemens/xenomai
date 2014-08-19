obj-$(CONFIG_XENOMAI) += posix.o

posix-y :=		\
	clock.o		\
	cond.o		\
	event.o		\
	init.o		\
	io.o		\
	monitor.o	\
	mqueue.o	\
	mutex.o		\
	nsem.o		\
	process.o	\
	sched.o		\
	sem.o		\
	signal.o	\
	syscall.o	\
	thread.o	\
	timer.o		\
	timerfd.o

ccflags-y := -Iarch/$(SRCARCH)/xenomai/include -Iinclude/xenomai -Ikernel
