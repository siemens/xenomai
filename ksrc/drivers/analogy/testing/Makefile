ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai

obj-$(CONFIG_XENO_DRIVERS_ANALOGY_FAKE) += analogy_fake.o

analogy_fake-y := fake.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_ANALOGY_FAKE) += analogy_fake.o

analogy_fake-objs := fake.o

export-objs := $(analogy_fake-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

analogy_fake.o: $(analogy_fake-objs)
	$(LD) -r -o $@ $(analogy_fake-objs)

endif
