ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_VRTX) += xeno_vrtx.o

xeno_vrtx-y := event.o heap.o mb.o module.o mx.o pt.o queue.o sem.o task.o time.o

else

# Makefile frag for Linux v2.4

O_TARGET := xeno_vrtx.o

obj-$(CONFIG_XENO_SKIN_VRTX) := event.o heap.o mb.o module.o mx.o pt.o queue.o sem.o task.o time.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
