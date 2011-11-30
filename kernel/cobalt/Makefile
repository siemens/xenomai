
obj-$(CONFIG_XENOMAI) += nucleus/ rtdm/ xeno_cobalt.o

xeno_cobalt-y := sched.o thread_attr.o thread.o mutex_attr.o mutex.o \
		cond_attr.o cond.o sem.o cancel.o once.o signal.o tsd.o \
		clock.o timer.o registry.o mq.o module.o apc.o syscall.o monitor.o

xeno_cobalt-$(CONFIG_XENO_OPT_POSIX_SHM) += shm.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
