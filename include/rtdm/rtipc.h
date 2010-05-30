/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org> 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * @ingroup rtipc
 */

/*!
 * @ingroup profiles
 * @defgroup rtipc Real-time IPC Devices
 *
 * @b Profile @b Revision: 1
 * @n
 * @n
 * @par Device Characteristics
 * @n
 * @ref rtdm_device.device_flags "Device Flags": @c RTDM_PROTOCOL_DEVICE @n
 * @n
 * @ref rtdm_device.protocol_family "Protocol Family": @c PF_RTIPC @n
 * @n
 * @ref rtdm_device.socket_type "Socket Type": @c SOCK_DGRAM @n
 * @n
 * @ref rtdm_device.device_class "Device Class": @c RTDM_CLASS_RTIPC @n
 * @n
 */

#ifndef _RTIPC_H

#ifdef __KERNEL__

#include <linux/net.h>
#include <linux/socket.h>
#include <linux/if.h>

#else  /* !__KERNEL__ */

#include <sys/types.h>
#include <sys/socket.h>

#endif /* !__KERNEL__ */

#include <nucleus/types.h>
#include <rtdm/rtdm.h>

/* Address family */
#define AF_RTIPC		111

/* Protocol family */
#define PF_RTIPC		AF_RTIPC

enum {
	IPCPROTO_IPC  = 0,	/* Default protocol (IDDP) */
	IPCPROTO_XDDP = 1,	/* Cross-domain datagram protocol */
	IPCPROTO_IDDP = 2,	/* Intra-domain datagram protocol */
	IPCPROTO_BUFP = 3,	/* Buffer protocol */
	IPCPROTO_MAX
};

 /*
  * Valid port ranges:
  * XDDP = [0..OPT_PIPE_NRDEV-1]
  * IDDP = [0..OPT_IDDP_NRPORT-1]
  * BUFP = [0..OPT_BUFP_NRPORT-1]
  */
typedef int16_t rtipc_port_t;

struct rtipc_port_label {
	char label[XNOBJECT_NAME_LEN];
};

struct sockaddr_ipc {
	sa_family_t sipc_family; /* AF_RTIPC */
	rtipc_port_t sipc_port;
};

/* Socket level options (via set/getsockopt) */
#define SOL_XDDP		311
#define XDDP_LABEL		1
#define XDDP_POOLSZ		2
#define XDDP_BUFSZ		3
#define XDDP_MONITOR		4

#define SOL_IDDP		312
#define IDDP_LABEL		1
#define IDDP_POOLSZ		2

#define SOL_BUFP		313
#define BUFP_LABEL		1
#define BUFP_BUFSZ		2

/* XDDP in-kernel monitored events */
#define XDDP_EVTIN	1
#define XDDP_EVTOUT	2
#define XDDP_EVTDOWN	3
#define XDDP_EVTNOBUF	4

#endif /* !_RTIPC_H */
