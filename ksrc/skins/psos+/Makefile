ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_PSOS) += xeno_psos.o

xeno_psos-y := module.o asr.o event.o pt.o queue.o rn.o task.o tm.o sem.o

else

# Makefile frag for Linux v2.4

O_TARGET := xeno_psos.o

obj-$(CONFIG_XENO_SKIN_PSOS) := module.o asr.o event.o pt.o queue.o rn.o task.o tm.o sem.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
