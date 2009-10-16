ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai

obj-$(CONFIG_XENO_DRIVERS_ANALOGY_8255) += analogy_8255.o

analogy_8255-y := 8255.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_ANALOGY_8255) += analogy_8255.o

analogy_8255-objs := 8255.o

export-objs := $(analgoy_8255-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

analogy_8255.o: $(analogy_8255-objs)
	$(LD) -r -o $@ $(analogy_8255-objs)

endif
