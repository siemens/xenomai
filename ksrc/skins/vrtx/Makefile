
obj-$(CONFIG_XENO_SKIN_VRTX) += xeno_vrtx.o

xeno_vrtx-y := event.o heap.o mb.o module.o mx.o pt.o queue.o sem.o task.o time.o

clean-files := $(patsubst $(obj)/%,%,		\
		$(wildcard $(obj)/*.o)		\
		$(wildcard $(obj)/.*.d)		\
		$(wildcard $(obj)/.*.cmd)	\
		$(wildcard $(obj)/*.mod.c)	\
		$(wildcard $(obj)/*.ko))
