ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_UITRON) += xeno_uitron.o

xeno_uitron-y := flag.o mbx.o module.o sem.o task.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_SKIN_UITRON) := xeno_uitron.o

list-multi := xeno_uitron.o

xeno_uitron-objs := flag.o mbx.o module.o sem.o task.o

export-objs := $(xeno_uitron-objs)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

xeno_uitron.o: $(xeno_uitron-objs)
	$(LD) -r -o $@ $(xeno_uitron-objs)

endif
