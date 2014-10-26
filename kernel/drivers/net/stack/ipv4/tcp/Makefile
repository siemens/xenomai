ccflags-y := -Iarch/$(SRCARCH)/xenomai/include -Iinclude/xenomai \
	-Idrivers/xenomai/net/stack/include

obj-$(CONFIG_XENO_DRIVERS_NET_RTIPV4_TCP) += rttcp.o

rttcp-y := \
	tcp.o \
	timerwheel.o
