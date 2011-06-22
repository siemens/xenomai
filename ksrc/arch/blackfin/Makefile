obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o switch.o

ifndef CONFIG_SMP
xeno_hal-y += atomic.o
endif

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai
