ccflags-y := -Iarch/$(SRCARCH)/xenomai/include -Iinclude/xenomai \
	-Idrivers/xenomai/net/stack/include

obj-$(CONFIG_XENO_DRIVERS_NET_RTPACKET) += rtpacket.o

rtpacket-y := af_packet.o
