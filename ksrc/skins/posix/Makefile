
obj-$(CONFIG_XENO_SKIN_POSIX) += xeno_posix.o

xeno_posix-y := sched.o thread_attr.o thread.o mutex_attr.o mutex.o cond_attr.o cond.o sem.o \
		cancel.o once.o signal.o tsd.o clock.o timer.o registry.o mq.o module.o

xeno_posix-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o intr.o

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.d)		\
		$(wildcard $(obj)/.*.cmd)	\
		$(wildcard $(obj)/*.mod.c)	\
		$(wildcard $(obj)/*.ko))
