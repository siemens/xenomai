ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_OPT_NUCLEUS) += xeno_nucleus.o

xeno_nucleus-y := heap.o intr.o module.o pod.o synch.o thread.o timer.o

xeno_nucleus-$(CONFIG_XENO_OPT_PERVASIVE) += shadow.o core.o

xeno_nucleus-$(CONFIG_XENO_OPT_PIPE) += pipe.o

xeno_nucleus-$(CONFIG_LTT) += ltt.o

else

# Makefile frag for Linux v2.4

O_TARGET := xeno_nucleus.o

obj-$(CONFIG_XENO_OPT_NUCLEUS) := heap.o intr.o module.o pod.o synch.o thread.o timer.o

obj-$(CONFIG_XENO_OPT_PERVASIVE) += shadow.o core.o

obj-$(CONFIG_XENO_OPT_PIPE) += pipe.o

obj-$(CONFIG_LTT) += ltt.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
