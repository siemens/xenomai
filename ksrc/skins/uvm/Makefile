ifeq ($(PATCHLEVEL),6)

# Makefile frag for Linux v2.6

obj-$(CONFIG_XENO_SKIN_UVM) += xeno_uvm.o

xeno_uvm-y := module.o syscall.o

else

# Makefile frag for Linux v2.4

O_TARGET := xeno_uvm.o

obj-$(CONFIG_XENO_SKIN_UVM) := module.o syscall.o

export-objs := $(obj-y) $(obj-m)

EXTRA_CFLAGS += -I$(TOPDIR)/include/xenomai/compat

include $(TOPDIR)/Rules.make

endif
