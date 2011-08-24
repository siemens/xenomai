
EXTRA_CFLAGS += -D__IN_XENOMAI__ -Iinclude/xenomai -Idrivers/xenomai/can -Idrivers/xenomai/can/mscan

obj-$(CONFIG_XENO_DRIVERS_CAN_MSCAN) += xeno_can_mscan.o

xeno_can_mscan-y := rtcan_mscan.o rtcan_mscan_proc.o
ifdef CONFIG_XENO_DRIVERS_CAN_MSCAN_OLD
xeno_can_mscan-y += rtcan_mscan_mpc52xx.o
else
xeno_can_mscan-y += rtcan_mscan_mpc5xxx.o
endif
