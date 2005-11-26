ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_VRTX) += xeno_vrtx.o

xeno_vrtx-y := event.o heap.o mb.o module.o mx.o pt.o queue.o sem.o task.o time.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_SKIN_VRTX) := xeno_vrtx.o

list-multi := xeno_vrtx.o

xeno_vrtx-objs := event.o heap.o mb.o module.o mx.o pt.o queue.o sem.o task.o time.o

export-objs := $(xeno_vrtx-objs)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

xeno_vrtx.o: $(xeno_vrtx-objs)
	$(LD) -r -o $@ $(xeno_vrtx-objs)

endif
