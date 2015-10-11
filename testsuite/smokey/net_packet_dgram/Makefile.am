noinst_LIBRARIES = libnet_packet_dgram.a

libnet_packet_dgram_a_SOURCES = \
	packet_dgram.c

libnet_packet_dgram_a_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(srcdir)/../net_common \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/kernel/drivers/net/stack/include
