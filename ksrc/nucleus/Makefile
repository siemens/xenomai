
obj-$(CONFIG_XENO_OPT_NUCLEUS) += xeno_nucleus.o

xeno_nucleus-y := hal.o heap.o intr.o module.o pod.o synch.o thread.o timer.o

xeno_nucleus-$(CONFIG_XENO_OPT_PERVASIVE) += shadow.o core.o

xeno_nucleus-$(CONFIG_XENO_OPT_PIPE) += pipe.o

xeno_nucleus-$(CONFIG_LTT) += ltt.o
