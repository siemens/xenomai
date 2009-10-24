
obj-$(CONFIG_XENOMAI) += xeno_hal.o

xeno_hal-y := hal.o switch.o

ifndef CONFIG_SMP
xeno_hal-y += atomic.o
endif

xeno_hal-$(CONFIG_XENO_HW_NMI_DEBUG_LATENCY) += nmi.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai
