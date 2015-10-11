/*
 * RTnet test server
 *
 * Copyright (C) 2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SMOKEY_NET_CHECK_H
#define SMOKEY_NET_CHECK_H

#define check_native(expr)                             \
       smokey_net_server_check_inner(__FILE__, __LINE__, #expr, (expr))

#define check_pthread(expr)                            \
       smokey_net_server_check_inner(__FILE__, __LINE__, #expr, -(expr))

#define check_unix(expr)						\
      ({								\
	       int s = (expr);						\
	       smokey_net_server_check_inner(__FILE__, __LINE__, #expr, s < 0 ? -errno : s); \
       })

struct smokey_server;

int smokey_net_server_check_inner(const char *file, int line,
				     const char *msg, int status);

void smokey_net_server_loop(int net_config);

#endif /* SMOKEY_NET_CHECK_H */
