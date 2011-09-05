ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai

obj-$(CONFIG_XENO_DRIVERS_ANALOGY_S526) += analogy_s526.o

analogy_s526-y := s526.o

else

# Makefile flag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_ANALOGY_S526) += analogy_s526.o

analogy_s526-objs := s526.o

export-objs := $(analogy_s526-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

analogy_s526.o: $(analogy_s526-objs)
	$(LD) -r -o $@ $(analogy_s526-objs)

endif
