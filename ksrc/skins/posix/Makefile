
obj-$(CONFIG_XENO_SKIN_POSIX) += xeno_posix.o

xeno_posix-y := sched.o thread_attr.o thread.o mutex_attr.o mutex.o \
		cond_attr.o cond.o sem.o cancel.o once.o signal.o tsd.o \
		clock.o timer.o registry.o mq.o module.o apc.o

xeno_posix-$(CONFIG_XENO_OPT_POSIX_SHM) += shm.o

xeno_posix-$(CONFIG_XENO_OPT_POSIX_INTR) += intr.o

xeno_posix-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(srctree)/include/xenomai/posix -I$(src)/.. -Iinclude/xenomai
