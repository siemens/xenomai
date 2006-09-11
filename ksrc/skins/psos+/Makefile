ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_PSOS) += xeno_psos.o

xeno_psos-y := module.o asr.o event.o pt.o queue.o rn.o task.o tm.o sem.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_SKIN_PSOS) := xeno_psos.o

list-multi := xeno_psos.o

xeno_psos-objs := module.o asr.o event.o pt.o queue.o rn.o task.o tm.o sem.o

export-objs := $(xeno_psos-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

xeno_psos.o: $(xeno_psos-objs)
	$(LD) -r -o $@ $(xeno_psos-objs)

endif
