/*
 * Copyright (C) 2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SMOKEY_NET_H
#define SMOKEY_NET_H

#include <time.h>
#include <sys/socket.h>
#include <netpacket/packet.h>
#include <netinet/in.h>

#include <smokey/smokey.h>

#ifndef ETH_P_802_EX1
#define ETH_P_802_EX1	0x88B5		/* 802.1 Local Experimental 1.  */
#endif

struct smokey_net_payload {
	struct timespec ts;
	unsigned seq;
};

struct smokey_net_client {
	const char *name;
	int option;
	union {
		struct sockaddr peer;
		struct sockaddr_ll ll_peer;
		struct sockaddr_in in_peer;
	};
	socklen_t peer_len;

	int (*create_socket)(struct smokey_net_client *client);
	int (*prepare)(struct smokey_net_client *client,
		void *buf, size_t len,
		const struct smokey_net_payload *payload);
	int (*extract)(struct smokey_net_client *client,
		struct smokey_net_payload *payload,
		const void *buf, size_t len);
};

int smokey_net_setup(const char *driver, const char *intf, int tested_config,
		void *vpeer);

int smokey_net_teardown(const char *driver,
			const char *intf, int tested_config);

int smokey_net_client_run(struct smokey_test *t,
			struct smokey_net_client *client,
			int argc, char *const argv[]);

#endif /* SMOKEY_NET_H */
