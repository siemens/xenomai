ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_UVM) += xeno_uvm.o

xeno_uvm-y := module.o syscall.o

EXTRA_CFLAGS += -Iinclude/xenomai

else

# Makefile frag for Linux v2.4

O_TARGET := built-in.o

obj-$(CONFIG_XENO_SKIN_UVM) := xeno_uvm.o

list-multi := xeno_uvm.o

xeno_uvm-objs := module.o syscall.o

export-objs := $(xeno_uvm-objs)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

xeno_uvm.o: $(xeno_uvm-objs)
	$(LD) -r -o $@ $(xeno_uvm-objs)

endif
