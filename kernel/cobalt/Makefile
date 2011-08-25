
obj-$(CONFIG_XENOMAI) += nucleus/ rtdm/ native/ xeno_posix.o

xeno_posix-y := sched.o thread_attr.o thread.o mutex_attr.o mutex.o \
		cond_attr.o cond.o sem.o cancel.o once.o signal.o tsd.o \
		clock.o timer.o registry.o mq.o module.o apc.o syscall.o

xeno_posix-$(CONFIG_XENO_OPT_POSIX_SHM) += shm.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai
