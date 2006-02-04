ifeq ($(PATCHLEVEL),6)

EXTRA_CFLAGS += -Iinclude/xenomai

EXTRA_AFLAGS += -Iarch/ia64/kernel

obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o switch.o

endif
