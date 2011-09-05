ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai -Idrivers/xenomai/ipc

obj-$(CONFIG_XENO_DRIVERS_RTIPC) += xeno_rtipc.o

xeno_rtipc-y := rtipc.o

xeno_rtipc-$(CONFIG_XENO_DRIVERS_RTIPC_XDDP) += xddp.o
xeno_rtipc-$(CONFIG_XENO_DRIVERS_RTIPC_IDDP) += iddp.o
xeno_rtipc-$(CONFIG_XENO_DRIVERS_RTIPC_BUFP) += bufp.o

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_DRIVERS_RTIPC) += xeno_rtipc.o

xeno_rtipc-objs := rtipc.o

opt_objs-y :=
opt_objs-$(CONFIG_XENO_DRIVERS_RTIPC_XDDP) += xddp.o
opt_objs-$(CONFIG_XENO_DRIVERS_RTIPC_IDDP) += iddp.o
opt_objs-$(CONFIG_XENO_DRIVERS_RTIPC_BUFP) += bufp.o

xeno_rtipc-objs += $(opt_objs-y)

export-objs := $(xeno_rtipc-objs)

EXTRA_CFLAGS += -D__IN_XENOMAI__ -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat -I.

include $(TOPDIR)/Rules.make

xeno_rtipc.o: $(xeno_rtipc-objs)
	$(LD) -r -o $@ $(xeno_rtipc-objs)

endif
