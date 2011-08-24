/**
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
 */

#ifndef _RTIPC_INTERNAL_H
#define _RTIPC_INTERNAL_H

#include <nucleus/registry.h>
#include <rtdm/rtdm.h>
#include <rtdm/rtdm_driver.h>

#define RTIPC_IOV_MAX  64

struct rtipc_protocol;

struct rtipc_private {
	struct rtipc_protocol *proto;
	void *state;
};

struct rtipc_protocol {
	const char *proto_name;
	int proto_statesz;
	int (*proto_init)(void);
	void (*proto_exit)(void);
	struct {
		int (*socket)(struct rtipc_private *priv,
			      rtdm_user_info_t *user_info);
		int (*close)(struct rtipc_private *priv,
			     rtdm_user_info_t *user_info);
		ssize_t (*recvmsg)(struct rtipc_private *priv,
				   rtdm_user_info_t *user_info,
				   struct msghdr *msg, int flags);
		ssize_t (*sendmsg)(struct rtipc_private *priv,
				   rtdm_user_info_t *user_info,
				   const struct msghdr *msg, int flags);
		ssize_t (*read)(struct rtipc_private *priv,
				rtdm_user_info_t *user_info,
				void *buf, size_t len);
		ssize_t (*write)(struct rtipc_private *priv,
				 rtdm_user_info_t *user_info,
				 const void *buf, size_t len);
		int (*ioctl)(struct rtipc_private *priv,
			     rtdm_user_info_t *user_info,
			     unsigned int request, void *arg);
	} proto_ops;
};

static inline void *rtipc_context_to_state(struct rtdm_dev_context *context)
{
	struct rtipc_private *p = rtdm_context_to_private(context);
	return p->state;
}

static inline void *rtipc_fd2map(int fd)
{
	return (void *)(long)(fd + 1);
}

static inline int rtipc_map2fd(void *p)
{
	return (long)p - 1;
}

static inline nanosecs_rel_t rtipc_timeval_to_ns(const struct timeval *tv)
{
	nanosecs_rel_t ns = tv->tv_usec * 1000;

	if (tv->tv_sec)
		ns += (nanosecs_rel_t)tv->tv_sec * 1000000000UL;

	return ns;
}

static inline void rtipc_ns_to_timeval(struct timeval *tv, nanosecs_rel_t ns)
{
	unsigned long nsecs;

	tv->tv_sec = xnarch_divrem_billion(ns, &nsecs);
	tv->tv_usec = nsecs / 1000;
}

int rtipc_get_arg(rtdm_user_info_t *user_info,
		  void *dst, const void *src, size_t len);

int rtipc_put_arg(rtdm_user_info_t *user_info,
		  void *dst, const void *src, size_t len);

int rtipc_get_sockaddr(rtdm_user_info_t *user_info,
		       const void *arg, struct sockaddr_ipc **saddrp);

int rtipc_put_sockaddr(rtdm_user_info_t *user_info, void *arg,
		       const struct sockaddr_ipc *saddr);

ssize_t rtipc_get_iov_flatlen(struct iovec *iov, int iovlen);

extern struct rtipc_protocol xddp_proto_driver;

extern struct rtipc_protocol iddp_proto_driver;

extern struct rtipc_protocol bufp_proto_driver;

extern struct xnptree rtipc_ptree;

#define rtipc_wait_context		xnthread_wait_context
#define rtipc_prepare_wait		xnthread_prepare_wait
#define rtipc_finish_wait		xnthread_finish_wait
#define rtipc_get_wait_context		xnthread_get_wait_context

#define rtipc_peek_wait_head(obj)	xnsynch_peek_pendq(&(obj)->synch_base)
#define rtipc_enter_atomic(lockctx)	xnlock_get_irqsave(&nklock, (lockctx))
#define rtipc_leave_atomic(lockctx)	xnlock_put_irqrestore(&nklock, (lockctx))

#endif /* !_RTIPC_INTERNAL_H */
