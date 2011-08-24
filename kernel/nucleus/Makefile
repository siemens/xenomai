
obj-$(CONFIG_XENO_OPT_NUCLEUS) += xeno_nucleus.o

xeno_nucleus-y := \
	bufd.o heap.o intr.o pod.o registry.o \
	synch.o thread.o timebase.o timer.o shadow.o \
	sched.o sched-idle.o sched-rt.o

xeno_nucleus-$(CONFIG_XENO_OPT_SCHED_SPORADIC) += sched-sporadic.o
xeno_nucleus-$(CONFIG_XENO_OPT_SCHED_TP) += sched-tp.o

xeno_nucleus-$(CONFIG_XENO_OPT_PIPE) += pipe.o
xeno_nucleus-$(CONFIG_XENO_OPT_MAP) += map.o
xeno_nucleus-$(CONFIG_XENO_OPT_SELECT) += select.o
xeno_nucleus-$(CONFIG_XENO_OPT_DEBUG) += debug.o
xeno_nucleus-$(CONFIG_PROC_FS) += vfile.o

# CAUTION: this module shall appear last, so that dependencies may
# exist on initcalls defined by other object files.
xeno_nucleus-y += module.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai
