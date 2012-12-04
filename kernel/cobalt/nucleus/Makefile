
obj-$(CONFIG_XENOMAI) += xeno_nucleus.o

xeno_nucleus-y := \
	apc.o arith.o bufd.o heap.o intr.o lock.o pod.o \
	 registry.o synch.o thread.o clock.o timer.o shadow.o \
	sched.o sched-idle.o sched-rt.o select.o

xeno_nucleus-$(CONFIG_XENO_OPT_SCHED_SPORADIC) += sched-sporadic.o
xeno_nucleus-$(CONFIG_XENO_OPT_SCHED_TP) += sched-tp.o

xeno_nucleus-$(CONFIG_XENO_OPT_PIPE) += pipe.o
xeno_nucleus-$(CONFIG_XENO_OPT_MAP) += map.o
xeno_nucleus-$(CONFIG_XENO_OPT_DEBUG) += debug.o
xeno_nucleus-$(CONFIG_PROC_FS) += vfile.o

# CAUTION: this module shall appear last, so that dependencies may
# exist on initcalls defined by other object files.
xeno_nucleus-y += module.o

ccflags-y := -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt
