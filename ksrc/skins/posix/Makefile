ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_POSIX) += xeno_posix.o

xeno_posix-y := sched.o thread_attr.o thread.o mutex_attr.o mutex.o cond_attr.o cond.o sem.o \
		cancel.o once.o signal.o tsd.o clock.o timer.o registry.o mq.o module.o

xeno_posix-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o intr.o

else

# Makefile frag for Linux v2.4

O_TARGET := xeno_posix.o

obj-y := sched.o thread_attr.o thread.o mutex_attr.o mutex.o cond_attr.o cond.o sem.o \
	 cancel.o once.o signal.o tsd.o clock.o timer.o registry.o mq.o module.o

obj-$(CONFIG_XENO_OPT_PERVASIVE) += syscall.o intr.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
