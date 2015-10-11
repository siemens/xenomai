noinst_LIBRARIES = libnet_udp.a

libnet_udp_a_SOURCES = \
	udp.c

libnet_udp_a_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(srcdir)/../net_common \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/kernel/drivers/net/stack/include
