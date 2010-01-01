
EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai

obj-$(CONFIG_XENO_DRIVERS_TIMERBENCH) += xeno_timerbench.o
obj-$(CONFIG_XENO_DRIVERS_IRQBENCH)   += xeno_irqbench.o
obj-$(CONFIG_XENO_DRIVERS_SWITCHTEST) += xeno_switchtest.o
obj-$(CONFIG_XENO_DRIVERS_KLATENCY)   += xeno_klat.o
obj-$(CONFIG_XENO_DRIVERS_SIGTEST)    += xeno_sigtest.o
obj-$(CONFIG_XENO_DRIVERS_RTDMTEST)   += xeno_rtdmtest.o

xeno_timerbench-y := timerbench.o

xeno_irqbench-y := irqbench.o

xeno_switchtest-y := switchtest.o

xeno_klat-y := klat.o

xeno_sigtest-y := sigtest_module.o

xeno_rtdmtest-y := rtdmtest.o

EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai
