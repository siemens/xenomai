/*
 * RTnet UDP test
 *
 * Copyright (C) 2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <netinet/in.h>

#include <sys/cobalt.h>
#include <smokey/smokey.h>
#include "smokey_net.h"

smokey_test_plugin(net_udp,
	SMOKEY_ARGLIST(
		SMOKEY_STRING(rtnet_driver),
		SMOKEY_STRING(rtnet_interface),
		SMOKEY_INT(rtnet_rate),
		SMOKEY_INT(rtnet_duration),
	),
	"Check RTnet driver, using UDP packets, measuring round trip time\n"
	"\tand packet losses,\n"
	"\tthe rtnet_driver parameter allows choosing the network driver\n"
	"\tthe rtnet_interface parameter allows choosing the network interface\n"
	"\tthe rtnet_rate parameter allows choosing the packet rate\n"
	"\tthe rtnet_duration parameter allows choosing the test duration\n"
	"\tA server on the network must run the smokey_rtnet_server program."
);

static int
udp_create_socket(struct smokey_net_client *client)
{
	return smokey_check_errno(__RT(socket(PF_INET, SOCK_DGRAM, 0)));
}

static int
udp_prepare(struct smokey_net_client *client,
		void *buf, size_t len, const struct smokey_net_payload *payload)
{
	if (sizeof(*payload) < len)
		len = sizeof(*payload);
	memcpy(buf, payload, len);
	return len;
}

static int
udp_extract(struct smokey_net_client *client,
		struct smokey_net_payload *payload, const void *buf, size_t len)
{
	if (sizeof(*payload) < len)
		len = sizeof(*payload);
	memcpy(payload, buf, len);
	return len;
}

static int
run_net_udp(struct smokey_test *t, int argc, char *const argv[])
{
	struct smokey_net_client client = {
		.name = "UDP",
		.option = _CC_COBALT_NET_UDP,
		.create_socket = &udp_create_socket,
		.prepare = &udp_prepare,
		.extract = &udp_extract,
	};

	memset(&client.in_peer, '\0', sizeof(client.in_peer));
	client.in_peer.sin_family = AF_INET;
	client.in_peer.sin_port = htons(7); /* UDP echo port */
	client.in_peer.sin_addr.s_addr = htonl(INADDR_ANY);
	client.peer_len = sizeof(client.in_peer);

	return smokey_net_client_run(t, &client, argc, argv);
}
