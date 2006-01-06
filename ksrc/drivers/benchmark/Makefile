ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

EXTRA_CFLAGS += -Iinclude/xenomai

obj-$(CONFIG_XENO_DRIVERS_TIMERBENCH) += xeno_timerbench.o

xeno_timerbench-y := timerbench.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_TIMERBENCH) := xeno_timerbench.o

list-multi := xeno_timerbench.o

xeno_timerbench-objs := timerbench.o

export-objs := $(xeno_timerbench-objs)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat -I$(TOPDIR)/include/xenomai

include $(TOPDIR)/Rules.make

xeno_timerbench.o: $(xeno_timerbench-objs)
	$(LD) -r -o $@ $(xeno_timerbench-objs)

endif
