noinst_LIBRARIES = libnet_packet_raw.a

libnet_packet_raw_a_SOURCES = \
	packet_raw.c

libnet_packet_raw_a_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(srcdir)/../net_common \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/kernel/drivers/net/stack/include
