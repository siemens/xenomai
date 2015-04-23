
ccflags-y += -Ikernel

obj-$(CONFIG_XENOMAI) += xenomai.o

xenomai-y :=		\
	clock.o		\
	cond.o		\
	event.o		\
	io.o		\
	memory.o	\
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

xenomai-$(CONFIG_XENO_ARCH_SYS3264) += compat.o syscall32.o
