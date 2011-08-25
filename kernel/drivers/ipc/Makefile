
EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai/cobalt -Iinclude/xenomai -Idrivers/xenomai/ipc

obj-$(CONFIG_XENO_DRIVERS_RTIPC) += xeno_rtipc.o

xeno_rtipc-y := rtipc.o

xeno_rtipc-$(CONFIG_XENO_DRIVERS_RTIPC_XDDP) += xddp.o
xeno_rtipc-$(CONFIG_XENO_DRIVERS_RTIPC_IDDP) += iddp.o
xeno_rtipc-$(CONFIG_XENO_DRIVERS_RTIPC_BUFP) += bufp.o
