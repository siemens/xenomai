ccflags-y += -Idrivers/xenomai/net/stack/include

obj-$(CONFIG_XENO_DRIVERS_NET_DRV_IGB) += rt_igb.o

rt_igb-y :=  \
	igb_main.o \
	e1000_82575.o \
	e1000_mac.o \
	e1000_nvm.o \
	e1000_phy.o
