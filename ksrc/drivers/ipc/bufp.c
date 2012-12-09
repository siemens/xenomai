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

#include <linux/module.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <nucleus/heap.h>
#include <nucleus/map.h>
#include <nucleus/bufd.h>
#include <rtdm/rtipc.h>
#include "internal.h"

#define trace(m,a...) printk(KERN_WARNING "%s: " m "\n", __FUNCTION__, ##a)

#define BUFP_SOCKET_MAGIC 0xa61a61a6

struct bufp_socket {
	int magic;
	struct sockaddr_ipc name;
	struct sockaddr_ipc peer;

	void *bufmem;
	size_t bufsz;
	u_long status;
	xnhandle_t handle;
	char label[XNOBJECT_NAME_LEN];

	off_t rdoff;
	off_t wroff;
	size_t fillsz;
	u_long wrtoken;
	u_long rdtoken;
	rtdm_event_t i_event;
	rtdm_event_t o_event;

	nanosecs_rel_t rx_timeout;
	nanosecs_rel_t tx_timeout;

	struct rtipc_private *priv;
};

struct bufp_wait_context {
	struct rtipc_wait_context wc;
	size_t len;
	struct bufp_socket *sk;
	rtdm_lockctx_t lockctx;
};

static struct sockaddr_ipc nullsa = {
	.sipc_family = AF_RTIPC,
	.sipc_port = -1
};

static struct xnmap *portmap;

#define _BUFP_BINDING  0
#define _BUFP_BOUND    1

#ifdef CONFIG_XENO_OPT_VFILE

static char *__bufp_link_target(void *obj)
{
	struct bufp_socket *sk = obj;
	char *buf;

	/* XXX: older kernels don't have kasprintf(). */
	buf = kmalloc(32, GFP_KERNEL);
	if (buf == NULL)
		return buf;

	snprintf(buf, 32, "%d", sk->name.sipc_port);

	return buf;
}

extern struct xnptree rtipc_ptree;

static struct xnpnode_link __bufp_pnode = {
	.node = {
		.dirname = "bufp",
		.root = &rtipc_ptree,
		.ops = &xnregistry_vlink_ops,
	},
	.target = __bufp_link_target,
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_link __bufp_pnode = {
	.node = {
		.dirname = "bufp",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

static void __bufp_cleanup_handler(struct rtipc_wait_context *wc)
{
	struct bufp_wait_context *bufwc;
	/*
	 * Cancellation request is pending - release the lock we hold,
	 * we'll be vanishing away soon. Granted, we could avoid doing
	 * that, since we know that this particular lock is Xenomai's
	 * nklock, which may be held across rescheduling calls.
	 * Anyway, this illustrates how to use the cleanup handler of
	 * a wait context.
	 */
	bufwc = container_of(wc, struct bufp_wait_context, wc);
	rtipc_leave_atomic(bufwc->lockctx);
}

static int bufp_socket(struct rtipc_private *priv,
		       rtdm_user_info_t *user_info)
{
	struct bufp_socket *sk = priv->state;

	sk->magic = BUFP_SOCKET_MAGIC;
	sk->name = nullsa;	/* Unbound */
	sk->peer = nullsa;
	sk->bufmem = NULL;
	sk->bufsz = 0;
	sk->rdoff = 0;
	sk->wroff = 0;
	sk->fillsz = 0;
	sk->rdtoken = 0;
	sk->wrtoken = 0;
	sk->status = 0;
	sk->handle = 0;
	sk->rx_timeout = RTDM_TIMEOUT_INFINITE;
	sk->tx_timeout = RTDM_TIMEOUT_INFINITE;
	*sk->label = 0;
	rtdm_event_init(&sk->i_event, 0);
	rtdm_event_init(&sk->o_event, 0);
	sk->priv = priv;

	return 0;
}

static int bufp_close(struct rtipc_private *priv,
		      rtdm_user_info_t *user_info)
{
	struct bufp_socket *sk = priv->state;

	rtdm_event_destroy(&sk->i_event);
	rtdm_event_destroy(&sk->o_event);

	if (sk->name.sipc_port > -1)
		xnmap_remove(portmap, sk->name.sipc_port);

	if (sk->handle)
		xnregistry_remove(sk->handle);

	if (sk->bufmem)
		xnarch_free_host_mem(sk->bufmem, sk->bufsz);

	kfree(sk);

	return 0;
}

static ssize_t __bufp_readbuf(struct bufp_socket *sk,
			      struct xnbufd *bufd,
			      int flags)
{
	struct bufp_wait_context wait, *bufwc;
	struct rtipc_wait_context *wc;
	xnthread_t *waiter;
	rtdm_toseq_t toseq;
	ssize_t len, ret;
	size_t rbytes, n;
	u_long rdtoken;
	off_t rdoff;

	len = bufd->b_len;

	rtdm_toseq_init(&toseq, sk->rx_timeout);

	rtipc_enter_atomic(wait.lockctx);

redo:
	for (;;) {
		/*
		 * We should be able to read a complete message of the
		 * requested length, or block.
		 */
		if (sk->fillsz < len)
			goto wait;

		/*
		 * Draw the next read token so that we can later
		 * detect preemption.
		 */
		rdtoken = ++sk->rdtoken;

		/* Read from the buffer in a circular way. */
		rdoff = sk->rdoff;
		rbytes = len;

		do {
			if (rdoff + rbytes > sk->bufsz)
				n = sk->bufsz - rdoff;
			else
				n = rbytes;
			/*
			 * Release the lock while retrieving the data
			 * to keep latency low.
			 */
			rtipc_leave_atomic(wait.lockctx);
			ret = xnbufd_copy_from_kmem(bufd, sk->bufmem + rdoff, n);
			if (ret < 0)
				return ret;

			rtipc_enter_atomic(wait.lockctx);
			/*
			 * In case we were preempted while retrieving
			 * the message, we have to re-read the whole
			 * thing.
			 */
			if (sk->rdtoken != rdtoken) {
				xnbufd_reset(bufd);
				goto redo;
			}

			rdoff = (rdoff + n) % sk->bufsz;
			rbytes -= n;
		} while (rbytes > 0);

		sk->fillsz -= len;
		sk->rdoff = rdoff;
		ret = len;

		/*
		 * Wake up all threads pending on the output wait
		 * queue, if we freed enough room for the leading one
		 * to post its message.
		 */
		waiter = rtipc_peek_wait_head(&sk->o_event);
		if (waiter == NULL)
			goto out;

		wc = rtipc_get_wait_context(waiter);
		XENO_BUGON(NUCLEUS, wc == NULL);
		bufwc = container_of(wc, struct bufp_wait_context, wc);
		if (bufwc->len + sk->fillsz <= sk->bufsz)
			rtdm_event_pulse(&sk->o_event);
		/*
		 * We cannot fail anymore once some data has been
		 * copied via the buffer descriptor, so no need to
		 * check for any reason to invalidate the latter.
		 */
		goto out;

	wait:
		if (flags & MSG_DONTWAIT) {
			ret = -EWOULDBLOCK;
			break;
		}

		/*
		 * Check whether writers are already waiting for
		 * sending data, while we are about to wait for
		 * receiving some. In such a case, we have a
		 * pathological use of the buffer. We must allow for a
		 * short read to prevent a deadlock.
		 */
		if (sk->fillsz > 0 && rtipc_peek_wait_head(&sk->o_event)) {
			len = sk->fillsz;
			goto redo;
		}

		wait.len = len;
		wait.sk = sk;
		rtipc_prepare_wait(&wait.wc);
		/*
		 * Keep the nucleus lock across the wait call, so that
		 * we don't miss a pulse.
		 */
		ret = rtdm_event_timedwait(&sk->i_event,
					   sk->rx_timeout, &toseq);
		rtipc_finish_wait(&wait.wc, __bufp_cleanup_handler);

		if (unlikely(ret))
			break;
	}

out:
	rtipc_leave_atomic(wait.lockctx);

	return ret;
}

static ssize_t __bufp_recvmsg(struct rtipc_private *priv,
			      rtdm_user_info_t *user_info,
			      struct iovec *iov, int iovlen, int flags,
			      struct sockaddr_ipc *saddr)
{
	struct bufp_socket *sk = priv->state;
	ssize_t len, wrlen, vlen, ret;
	struct xnbufd bufd;
	int nvec;

	if (!test_bit(_BUFP_BOUND, &sk->status))
		return -EAGAIN;

	len = rtipc_get_iov_flatlen(iov, iovlen);
	if (len == 0)
		return 0;
	/*
	 * We may only return complete messages to readers, so there
	 * is no point in waiting for messages which are larger than
	 * what the buffer can hold.
	 */
	if (len > sk->bufsz)
		return -EINVAL;

	/*
	 * Write "len" bytes from the buffer to the vector cells. Each
	 * cell is handled as a separate message.
	 */
	for (nvec = 0, wrlen = len; nvec < iovlen && wrlen > 0; nvec++) {
		if (iov[nvec].iov_len == 0)
			continue;
		vlen = wrlen >= iov[nvec].iov_len ? iov[nvec].iov_len : wrlen;
#ifdef CONFIG_XENO_OPT_PERVASIVE
		if (user_info) {
			xnbufd_map_uread(&bufd, iov[nvec].iov_base, vlen);
			ret = __bufp_readbuf(sk, &bufd, flags);
			xnbufd_unmap_uread(&bufd);
		} else
#endif
		{
			xnbufd_map_kread(&bufd, iov[nvec].iov_base, vlen);
			ret = __bufp_readbuf(sk, &bufd, flags);
			xnbufd_unmap_kread(&bufd);
		}
		if (ret < 0)
			return ret;
		iov[nvec].iov_base += vlen;
		iov[nvec].iov_len -= vlen;
		wrlen -= vlen;
		if (ret < vlen)
			/* Short reads may happen in rare cases. */
			break;
	}

	/*
	 * There is no way to determine who the sender was since we
	 * process data in byte-oriented mode, so we just copy our own
	 * sockaddr to send back a valid address.
	 */
	if (saddr)
		*saddr = sk->name;

	return len - wrlen;
}

static ssize_t bufp_recvmsg(struct rtipc_private *priv,
			    rtdm_user_info_t *user_info,
			    struct msghdr *msg, int flags)
{
	struct iovec iov[RTIPC_IOV_MAX];
	struct sockaddr_ipc saddr;
	ssize_t ret;

	if (flags & ~MSG_DONTWAIT)
		return -EINVAL;

	if (msg->msg_name) {
		if (msg->msg_namelen < sizeof(struct sockaddr_ipc))
			return -EINVAL;
	} else if (msg->msg_namelen != 0)
		return -EINVAL;

	if (msg->msg_iovlen >= RTIPC_IOV_MAX)
		return -EINVAL;

	/* Copy I/O vector in */
	if (rtipc_get_arg(user_info, iov, msg->msg_iov,
			  sizeof(iov[0]) * msg->msg_iovlen))
		return -EFAULT;

	ret = __bufp_recvmsg(priv, user_info,
			     iov, msg->msg_iovlen, flags, &saddr);
	if (ret <= 0)
		return ret;

	/* Copy the updated I/O vector back */
	if (rtipc_put_arg(user_info, msg->msg_iov, iov,
			  sizeof(iov[0]) * msg->msg_iovlen))
		return -EFAULT;

	/* Copy the source address if required. */
	if (msg->msg_name) {
		if (rtipc_put_arg(user_info, msg->msg_name,
				  &saddr, sizeof(saddr)))
			return -EFAULT;
		msg->msg_namelen = sizeof(struct sockaddr_ipc);
	}

	return ret;
}

static ssize_t bufp_read(struct rtipc_private *priv,
			 rtdm_user_info_t *user_info,
			 void *buf, size_t len)
{
	struct iovec iov = { .iov_base = buf, .iov_len = len };
	return __bufp_recvmsg(priv, user_info, &iov, 1, 0, NULL);
}

static ssize_t __bufp_writebuf(struct bufp_socket *rsk,
			       struct bufp_socket *sk,
			       struct xnbufd *bufd,
			       int flags)
{
	struct bufp_wait_context wait, *bufwc;
	struct rtipc_wait_context *wc;
	xnthread_t *waiter;
	rtdm_toseq_t toseq;
	ssize_t len, ret;
	size_t wbytes, n;
	u_long wrtoken;
	off_t wroff;

	len = bufd->b_len;

	rtdm_toseq_init(&toseq, sk->rx_timeout);

	rtipc_enter_atomic(wait.lockctx);

redo:
	for (;;) {
		/*
		 * We should be able to write the entire message at
		 * once or block.
		 */
		if (rsk->fillsz + len > rsk->bufsz)
			goto wait;

		/*
		 * Draw the next write token so that we can later
		 * detect preemption.
		 */
		wrtoken = ++rsk->wrtoken;

		/* Write to the buffer in a circular way. */
		wroff = rsk->wroff;
		wbytes = len;

		do {
			if (wroff + wbytes > rsk->bufsz)
				n = rsk->bufsz - wroff;
			else
				n = wbytes;
			/*
			 * Release the lock while copying the data to
			 * keep latency low.
			 */
			rtipc_leave_atomic(wait.lockctx);
			ret = xnbufd_copy_to_kmem(rsk->bufmem + wroff, bufd, n);
			if (ret < 0)
				return ret;
			rtipc_enter_atomic(wait.lockctx);
			/*
			 * In case we were preempted while copying the
			 * message, we have to write the whole thing
			 * again.
			 */
			if (rsk->wrtoken != wrtoken) {
				xnbufd_reset(bufd);
				goto redo;
			}

			wroff = (wroff + n) % rsk->bufsz;
			wbytes -= n;
		} while (wbytes > 0);

		rsk->fillsz += len;
		rsk->wroff = wroff;
		ret = len;

		/*
		 * Wake up all threads pending on the input wait
		 * queue, if we accumulated enough data to feed the
		 * leading one.
		 */
		waiter = rtipc_peek_wait_head(&rsk->i_event);
		if (waiter == NULL)
			goto out;

		wc = rtipc_get_wait_context(waiter);
		XENO_BUGON(NUCLEUS, wc == NULL);
		bufwc = container_of(wc, struct bufp_wait_context, wc);
		if (bufwc->len <= rsk->fillsz)
			rtdm_event_pulse(&rsk->i_event);
		/*
		 * We cannot fail anymore once some data has been
		 * copied via the buffer descriptor, so no need to
		 * check for any reason to invalidate the latter.
		 */
		goto out;

	wait:
		if (flags & MSG_DONTWAIT) {
			ret = -EWOULDBLOCK;
			break;
		}

		wait.len = len;
		wait.sk = rsk;
		rtipc_prepare_wait(&wait.wc);
		/*
		 * Keep the nucleus lock across the wait call, so that
		 * we don't miss a pulse.
		 */
		ret = rtdm_event_timedwait(&rsk->o_event,
					   sk->tx_timeout, &toseq);
		rtipc_finish_wait(&wait.wc, __bufp_cleanup_handler);
		if (unlikely(ret))
			break;
	}

out:
	rtipc_leave_atomic(wait.lockctx);

	return ret;
}

static ssize_t __bufp_sendmsg(struct rtipc_private *priv,
			      rtdm_user_info_t *user_info,
			      struct iovec *iov, int iovlen, int flags,
			      const struct sockaddr_ipc *daddr)
{
	struct bufp_socket *sk = priv->state, *rsk;
	struct rtdm_dev_context *rcontext;
	ssize_t len, rdlen, vlen, ret = 0;
	struct xnbufd bufd;
	int nvec;
	void *p;

	len = rtipc_get_iov_flatlen(iov, iovlen);
	if (len == 0)
		return 0;

	p = xnmap_fetch_nocheck(portmap, daddr->sipc_port);
	if (p == NULL)
		return -ECONNRESET;

	rcontext = rtdm_context_get(rtipc_map2fd(p));
	if (rcontext == NULL)
		return -ECONNRESET;

	rsk = rtipc_context_to_state(rcontext);
	if (!test_bit(_BUFP_BOUND, &rsk->status)) {
		rtdm_context_unlock(rcontext);
		return -ECONNREFUSED;
	}

	/*
	 * We may only send complete messages, so there is no point in
	 * accepting messages which are larger than what the buffer
	 * can hold.
	 */
	if (len > rsk->bufsz) {
		ret = -EINVAL;
		goto fail;
	}

	/*
	 * Read "len" bytes to the buffer from the vector cells. Each
	 * cell is handled as a separate message.
	 */
	for (nvec = 0, rdlen = len; nvec < iovlen && rdlen > 0; nvec++) {
		if (iov[nvec].iov_len == 0)
			continue;
		vlen = rdlen >= iov[nvec].iov_len ? iov[nvec].iov_len : rdlen;
#ifdef CONFIG_XENO_OPT_PERVASIVE
		if (user_info) {
			xnbufd_map_uread(&bufd, iov[nvec].iov_base, vlen);
			ret = __bufp_writebuf(rsk, sk, &bufd, flags);
			xnbufd_unmap_uread(&bufd);
		} else
#endif
		{
			xnbufd_map_kread(&bufd, iov[nvec].iov_base, vlen);
			ret = __bufp_writebuf(rsk, sk, &bufd, flags);
			xnbufd_unmap_kread(&bufd);
		}
		if (ret < 0)
			goto fail;
		iov[nvec].iov_base += vlen;
		iov[nvec].iov_len -= vlen;
		rdlen -= vlen;
	}

	rtdm_context_unlock(rcontext);

	return len - rdlen;

fail:
	rtdm_context_unlock(rcontext);

	return ret;
}

static ssize_t bufp_sendmsg(struct rtipc_private *priv,
			    rtdm_user_info_t *user_info,
			    const struct msghdr *msg, int flags)
{
	struct bufp_socket *sk = priv->state;
	struct iovec iov[RTIPC_IOV_MAX];
	struct sockaddr_ipc daddr;
	ssize_t ret;

	if (flags & ~MSG_DONTWAIT)
		return -EINVAL;

	if (msg->msg_name) {
		if (msg->msg_namelen != sizeof(struct sockaddr_ipc))
			return -EINVAL;

		/* Fetch the destination address to send to. */
		if (rtipc_get_arg(user_info, &daddr,
				  msg->msg_name, sizeof(daddr)))
			return -EFAULT;

		if (daddr.sipc_port < 0 ||
		    daddr.sipc_port >= CONFIG_XENO_OPT_BUFP_NRPORT)
			return -EINVAL;
	} else {
		if (msg->msg_namelen != 0)
			return -EINVAL;
		daddr = sk->peer;
		if (daddr.sipc_port < 0)
			return -ENOTCONN;
	}

	if (msg->msg_iovlen >= RTIPC_IOV_MAX)
		return -EINVAL;

	/* Copy I/O vector in */
	if (rtipc_get_arg(user_info, iov, msg->msg_iov,
			  sizeof(iov[0]) * msg->msg_iovlen))
		return -EFAULT;

	ret = __bufp_sendmsg(priv, user_info, iov,
			     msg->msg_iovlen, flags, &daddr);
	if (ret <= 0)
		return ret;

	/* Copy updated I/O vector back */
	if (rtipc_put_arg(user_info, msg->msg_iov, iov,
			  sizeof(iov[0]) * msg->msg_iovlen))
		return -EFAULT;

	return ret;
}

static ssize_t bufp_write(struct rtipc_private *priv,
			  rtdm_user_info_t *user_info,
			  const void *buf, size_t len)
{
	struct iovec iov = { .iov_base = (void *)buf, .iov_len = len };
	struct bufp_socket *sk = priv->state;

	if (sk->peer.sipc_port < 0)
		return -EDESTADDRREQ;

	return __bufp_sendmsg(priv, user_info, &iov, 1, 0, &sk->peer);
}

static int __bufp_bind_socket(struct rtipc_private *priv,
			      struct sockaddr_ipc *sa)
{
	struct bufp_socket *sk = priv->state;
	int ret = 0, port, fd;

	if (sa->sipc_family != AF_RTIPC)
		return -EINVAL;

	if (sa->sipc_port < -1 ||
	    sa->sipc_port >= CONFIG_XENO_OPT_BUFP_NRPORT)
		return -EINVAL;

	RTDM_EXECUTE_ATOMICALLY(
		if (test_bit(_BUFP_BOUND, &sk->status) ||
		    __test_and_set_bit(_BUFP_BINDING, &sk->status))
			ret = -EADDRINUSE;
	);
	if (ret)
		return ret;

	/* Will auto-select a free port number if unspec (-1). */
	port = sa->sipc_port;
	fd = rtdm_private_to_context(priv)->fd;
	port = xnmap_enter(portmap, port, rtipc_fd2map(fd));
	if (port < 0)
		return port == -EEXIST ? -EADDRINUSE : -ENOMEM;

	sa->sipc_port = port;

	/*
	 * The caller must have told us how much memory is needed for
	 * buffer space via setsockopt(), before we got there.
	 */
	if (sk->bufsz == 0)
		return -ENOBUFS;

	sk->bufmem = xnarch_alloc_host_mem(sk->bufsz);
	if (sk->bufmem == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	sk->name = *sa;
	/* Set default destination if unset at binding time. */
	if (sk->peer.sipc_port < 0)
		sk->peer = *sa;

	if (*sk->label) {
		ret = xnregistry_enter(sk->label, sk,
				       &sk->handle, &__bufp_pnode.node);
		if (ret) {
			xnarch_free_host_mem(sk->bufmem, sk->bufsz);
			goto fail;
		}
	}

	RTDM_EXECUTE_ATOMICALLY(
		__clear_bit(_BUFP_BINDING, &sk->status);
		__set_bit(_BUFP_BOUND, &sk->status);
	);

	return 0;
fail:
	xnmap_remove(portmap, port);
	clear_bit(_BUFP_BINDING, &sk->status);

	return ret;
}

static int __bufp_connect_socket(struct bufp_socket *sk,
				 struct sockaddr_ipc *sa)
{
	struct bufp_socket *rsk;
	xnhandle_t h;
	int ret;

	if (sa == NULL) {
		sa = &nullsa;
		goto set_assoc;
	}

	if (sa->sipc_family != AF_RTIPC)
		return -EINVAL;

	if (sa->sipc_port < -1 ||
	    sa->sipc_port >= CONFIG_XENO_OPT_BUFP_NRPORT)
		return -EINVAL;
	/*
	 * - If a valid sipc_port is passed in the [0..NRPORT-1] range,
	 * it is used verbatim and the connection succeeds
	 * immediately, regardless of whether the destination is
	 * bound at the time of the call.
	 *
	 * - If sipc_port is -1 and a label was set via BUFP_LABEL,
	 * connect() blocks for the requested amount of time (see
	 * SO_RCVTIMEO) until a socket is bound to the same label.
	 *
	 * - If sipc_port is -1 and no label is given, the default
	 * destination address is cleared, meaning that any subsequent
	 * write() to the socket will return -EDESTADDRREQ, until a
	 * valid destination address is set via connect() or bind().
	 *
	 * - In all other cases, -EINVAL is returned.
	 */
	if (sa->sipc_port < 0 && *sk->label) {
		ret = xnregistry_bind(sk->label,
				      sk->rx_timeout, XN_RELATIVE, &h);
		if (ret)
			return ret;

		RTDM_EXECUTE_ATOMICALLY(
			rsk = xnregistry_fetch(h);
			if (rsk == NULL || rsk->magic != BUFP_SOCKET_MAGIC)
				ret = -EINVAL;
			else
				/* Fetch labeled port number. */
				sa->sipc_port = rsk->name.sipc_port;
		);
		if (ret)
			return ret;
	}

set_assoc:
	RTDM_EXECUTE_ATOMICALLY(
		if (!test_bit(_BUFP_BOUND, &sk->status))
			/* Set default name. */
			sk->name = *sa;
		/* Set default destination. */
		sk->peer = *sa;
	);

	return 0;
}

static int __bufp_setsockopt(struct bufp_socket *sk,
			     rtdm_user_info_t *user_info,
			     void *arg)
{
	struct _rtdm_setsockopt_args sopt;
	struct rtipc_port_label plabel;
	struct timeval tv;
	int ret = 0;
	size_t len;

	if (rtipc_get_arg(user_info, &sopt, arg, sizeof(sopt)))
		return -EFAULT;

	if (sopt.level == SOL_SOCKET) {
		switch (sopt.optname) {

		case SO_RCVTIMEO:
			if (sopt.optlen != sizeof(tv))
				return -EINVAL;
			if (rtipc_get_arg(user_info, &tv,
					  sopt.optval, sizeof(tv)))
				return -EFAULT;
			sk->rx_timeout = rtipc_timeval_to_ns(&tv);
			break;

		case SO_SNDTIMEO:
			if (sopt.optlen != sizeof(tv))
				return -EINVAL;
			if (rtipc_get_arg(user_info, &tv,
					  sopt.optval, sizeof(tv)))
				return -EFAULT;
			sk->tx_timeout = rtipc_timeval_to_ns(&tv);
			break;

		default:
			ret = -EINVAL;
		}

		return ret;
	}

	if (sopt.level != SOL_BUFP)
		return -ENOPROTOOPT;

	switch (sopt.optname) {

	case BUFP_BUFSZ:
		if (sopt.optlen != sizeof(len))
			return -EINVAL;
		if (rtipc_get_arg(user_info, &len,
				  sopt.optval, sizeof(len)))
			return -EFAULT;
		if (len == 0)
			return -EINVAL;
		RTDM_EXECUTE_ATOMICALLY(
			/*
			 * We may not do this more than once, and we
			 * have to do this before the first binding.
			 */
			if (test_bit(_BUFP_BOUND, &sk->status) ||
			    test_bit(_BUFP_BINDING, &sk->status))
				ret = -EALREADY;
			else
				sk->bufsz = len;
		);
		break;

	case BUFP_LABEL:
		if (sopt.optlen < sizeof(plabel))
			return -EINVAL;
		if (rtipc_get_arg(user_info, &plabel,
				  sopt.optval, sizeof(plabel)))
			return -EFAULT;
		RTDM_EXECUTE_ATOMICALLY(
			/*
			 * We may attach a label to a client socket
			 * which was previously bound in BUFP.
			 */
			if (test_bit(_BUFP_BINDING, &sk->status))
				ret = -EALREADY;
			else {
				strcpy(sk->label, plabel.label);
				sk->label[XNOBJECT_NAME_LEN-1] = 0;
			}
		);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int __bufp_getsockopt(struct bufp_socket *sk,
			     rtdm_user_info_t *user_info,
			     void *arg)
{
	struct _rtdm_getsockopt_args sopt;
	struct rtipc_port_label plabel;
	struct timeval tv;
	socklen_t len;
	int ret = 0;

	if (rtipc_get_arg(user_info, &sopt, arg, sizeof(sopt)))
		return -EFAULT;

	if (rtipc_get_arg(user_info, &len, sopt.optlen, sizeof(len)))
		return -EFAULT;

	if (sopt.level == SOL_SOCKET) {
		switch (sopt.optname) {

		case SO_RCVTIMEO:
			if (len != sizeof(tv))
				return -EINVAL;
			rtipc_ns_to_timeval(&tv, sk->rx_timeout);
			if (rtipc_put_arg(user_info, sopt.optval,
					  &tv, sizeof(tv)))
				return -EFAULT;
			break;

		case SO_SNDTIMEO:
			if (len != sizeof(tv))
				return -EINVAL;
			rtipc_ns_to_timeval(&tv, sk->tx_timeout);
			if (rtipc_put_arg(user_info, sopt.optval,
					  &tv, sizeof(tv)))
				return -EFAULT;
			break;

		default:
			ret = -EINVAL;
		}

		return ret;
	}

	if (sopt.level != SOL_BUFP)
		return -ENOPROTOOPT;

	switch (sopt.optname) {

	case BUFP_LABEL:
		if (len < sizeof(plabel))
			return -EINVAL;
		RTDM_EXECUTE_ATOMICALLY(
			strcpy(plabel.label, sk->label);
		);
		if (rtipc_put_arg(user_info, sopt.optval,
				  &plabel, sizeof(plabel)))
			return -EFAULT;
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int __bufp_ioctl(struct rtipc_private *priv,
			rtdm_user_info_t *user_info,
			unsigned int request, void *arg)
{
	struct sockaddr_ipc saddr, *saddrp = &saddr;
	struct bufp_socket *sk = priv->state;
	int ret = 0;

	switch (request) {

	case _RTIOC_CONNECT:
		ret = rtipc_get_sockaddr(user_info, arg, &saddrp);
		if (ret)
		  return ret;
		ret = __bufp_connect_socket(sk, saddrp);
		break;

	case _RTIOC_BIND:
		ret = rtipc_get_sockaddr(user_info, arg, &saddrp);
		if (ret)
			return ret;
		if (saddrp == NULL)
			return -EFAULT;
		ret = __bufp_bind_socket(priv, saddrp);
		break;

	case _RTIOC_GETSOCKNAME:
		ret = rtipc_put_sockaddr(user_info, arg, &sk->name);
		break;

	case _RTIOC_GETPEERNAME:
		ret = rtipc_put_sockaddr(user_info, arg, &sk->peer);
		break;

	case _RTIOC_SETSOCKOPT:
		ret = __bufp_setsockopt(sk, user_info, arg);
		break;

	case _RTIOC_GETSOCKOPT:
		ret = __bufp_getsockopt(sk, user_info, arg);
		break;

	case _RTIOC_LISTEN:
	case _RTIOC_ACCEPT:
		ret = -EOPNOTSUPP;
		break;

	case _RTIOC_SHUTDOWN:
		ret = -ENOTCONN;
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int bufp_ioctl(struct rtipc_private *priv,
		      rtdm_user_info_t *user_info,
		      unsigned int request, void *arg)
{
	if (rtdm_in_rt_context() && request == _RTIOC_BIND)
		return -ENOSYS;	/* Try downgrading to NRT */

	return __bufp_ioctl(priv, user_info, request, arg);
}

static int bufp_init(void)
{
	portmap = xnmap_create(CONFIG_XENO_OPT_BUFP_NRPORT, 0, 0);
	if (portmap == NULL)
		return -ENOMEM;

	return 0;
}

static void bufp_exit(void)
{
	xnmap_delete(portmap);
}

struct rtipc_protocol bufp_proto_driver = {
	.proto_name = "bufp",
	.proto_statesz = sizeof(struct bufp_socket),
	.proto_init = bufp_init,
	.proto_exit = bufp_exit,
	.proto_ops = {
		.socket = bufp_socket,
		.close = bufp_close,
		.recvmsg = bufp_recvmsg,
		.sendmsg = bufp_sendmsg,
		.read = bufp_read,
		.write = bufp_write,
		.ioctl = bufp_ioctl,
	}
};
