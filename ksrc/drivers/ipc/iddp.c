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
#include <nucleus/bufd.h>
#include <nucleus/map.h>
#include <rtdm/rtipc.h>
#include "internal.h"

#define trace(m,a...) printk(KERN_WARNING "%s: " m "\n", __FUNCTION__, ##a)

#define IDDP_SOCKET_MAGIC 0xa37a37a8

struct iddp_message {
	struct list_head next;
	int from;
	size_t rdoff;
	size_t len;
	char data[];
};

struct iddp_socket {
	int magic;
	struct sockaddr_ipc name;
	struct sockaddr_ipc peer;

	struct xnheap *bufpool;
	struct xnheap privpool;
	rtdm_event_t *poolevt;
	rtdm_event_t privevt;
	int *poolwait;
	int privwait;
	size_t poolsz;
	rtdm_sem_t insem;
	struct list_head inq;
	u_long status;
	xnhandle_t handle;
	char label[XNOBJECT_NAME_LEN];

	nanosecs_rel_t rx_timeout;
	nanosecs_rel_t tx_timeout;
	unsigned long stalls;	/* Buffer stall counter. */

	struct rtipc_private *priv;
};

static struct sockaddr_ipc nullsa = {
	.sipc_family = AF_RTIPC,
	.sipc_port = -1
};

static struct xnmap *portmap;

static rtdm_event_t poolevt;

static int poolwait;

#define _IDDP_BINDING  0
#define _IDDP_BOUND    1

#ifdef CONFIG_XENO_OPT_VFILE

static char *__iddp_link_target(void *obj)
{
	struct iddp_socket *sk = obj;
	char *buf;

	/* XXX: older kernels don't have kasprintf(). */
	buf = kmalloc(32, GFP_KERNEL);
	if (buf == NULL)
		return buf;

	snprintf(buf, 32, "%d", sk->name.sipc_port);

	return buf;
}

extern struct xnptree rtipc_ptree;

static struct xnpnode_link __iddp_pnode = {
	.node = {
		.dirname = "iddp",
		.root = &rtipc_ptree,
		.ops = &xnregistry_vlink_ops,
	},
	.target = __iddp_link_target,
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_link __iddp_pnode = {
	.node = {
		.dirname = "iddp",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

static inline void __iddp_init_mbuf(struct iddp_message *mbuf, size_t len)
{
	mbuf->rdoff = 0;
	mbuf->len = len;
	INIT_LIST_HEAD(&mbuf->next);
}

static struct iddp_message *
__iddp_alloc_mbuf(struct iddp_socket *sk, size_t len,
		  nanosecs_rel_t timeout, int flags, int *pret)
{
	struct iddp_message *mbuf = NULL;
	rtdm_toseq_t timeout_seq;
	int ret = 0;

	rtdm_toseq_init(&timeout_seq, timeout);

	for (;;) {
		mbuf = xnheap_alloc(sk->bufpool, len + sizeof(*mbuf));
		if (mbuf) {
			__iddp_init_mbuf(mbuf, len);
			break;
		}
		if (flags & MSG_DONTWAIT) {
			ret = -EAGAIN;
			break;
		}
		/*
		 * No luck, no buffer free. Wait for a buffer to be
		 * released and retry. Admittedly, we might create a
		 * thundering herd effect if many waiters put a lot of
		 * memory pressure on the pool, but in this case, the
		 * pool size should be adjusted.
		 */
		RTDM_EXECUTE_ATOMICALLY(
			/*
			 * membars are implicitly issued when required
			 * by this construct.
			 */
			++sk->stalls;
			(*sk->poolwait)++;
			ret = rtdm_event_timedwait(sk->poolevt,
						   timeout,
						   &timeout_seq);
			(*sk->poolwait)--;
			if (unlikely(ret == -EIDRM))
				ret = -ECONNRESET;
		);
		if (ret)
			break;
	}

	*pret = ret;

	return mbuf;
}

static void __iddp_free_mbuf(struct iddp_socket *sk,
			     struct iddp_message *mbuf)
{
	xnheap_free(sk->bufpool, mbuf);
	RTDM_EXECUTE_ATOMICALLY(
		/* Wake up sleepers if any. */
		if (*sk->poolwait > 0)
			rtdm_event_pulse(sk->poolevt);
	);
}

static void __iddp_flush_pool(struct xnheap *heap,
			      void *poolmem, u_long poolsz, void *cookie)
{
	xnarch_free_host_mem(poolmem, poolsz);
}

static int iddp_socket(struct rtipc_private *priv,
		       rtdm_user_info_t *user_info)
{
	struct iddp_socket *sk = priv->state;

	sk->magic = IDDP_SOCKET_MAGIC;
	sk->name = nullsa;	/* Unbound */
	sk->peer = nullsa;
	sk->bufpool = &kheap;
	sk->poolevt = &poolevt;
	sk->poolwait = &poolwait;
	sk->poolsz = 0;
	sk->status = 0;
	sk->handle = 0;
	sk->rx_timeout = RTDM_TIMEOUT_INFINITE;
	sk->tx_timeout = RTDM_TIMEOUT_INFINITE;
	sk->stalls = 0;
	*sk->label = 0;
	INIT_LIST_HEAD(&sk->inq);
	rtdm_sem_init(&sk->insem, 0);
	rtdm_event_init(&sk->privevt, 0);
	sk->priv = priv;

	return 0;
}

static int iddp_close(struct rtipc_private *priv,
		      rtdm_user_info_t *user_info)
{
	struct iddp_socket *sk = priv->state;
	struct iddp_message *mbuf;

	if (sk->name.sipc_port > -1)
		xnmap_remove(portmap, sk->name.sipc_port);

	rtdm_sem_destroy(&sk->insem);
	rtdm_event_destroy(&sk->privevt);

	if (sk->handle)
		xnregistry_remove(sk->handle);

	if (sk->bufpool != &kheap) {
		xnheap_destroy(&sk->privpool, __iddp_flush_pool, NULL);
		return 0;
	}

	/* Send unread datagrams back to the system heap. */
	while (!list_empty(&sk->inq)) {
		mbuf = list_entry(sk->inq.next, struct iddp_message, next);
		list_del(&mbuf->next);
		xnheap_free(&kheap, mbuf);
	}

	kfree(sk);

	return 0;
}

static ssize_t __iddp_recvmsg(struct rtipc_private *priv,
			      rtdm_user_info_t *user_info,
			      struct iovec *iov, int iovlen, int flags,
			      struct sockaddr_ipc *saddr)
{
	struct iddp_socket *sk = priv->state;
	ssize_t maxlen, len, wrlen, vlen;
	int nvec, rdoff, ret, dofree;
	struct iddp_message *mbuf;
	nanosecs_rel_t timeout;
	struct xnbufd bufd;

	if (!test_bit(_IDDP_BOUND, &sk->status))
		return -EAGAIN;

	maxlen = rtipc_get_iov_flatlen(iov, iovlen);
	if (maxlen == 0)
		return 0;

	/* We want to pick one buffer from the queue. */
	timeout = (flags & MSG_DONTWAIT) ? RTDM_TIMEOUT_NONE : sk->rx_timeout;
	ret = rtdm_sem_timeddown(&sk->insem, timeout, NULL);
	if (unlikely(ret)) {
		if (ret == -EIDRM)
			return -ECONNRESET;
		return ret;
	}

	RTDM_EXECUTE_ATOMICALLY(
		/* Pull heading message from input queue. */
		mbuf = list_entry(sk->inq.next, struct iddp_message, next);
		rdoff = mbuf->rdoff;
		len = mbuf->len - rdoff;
		if (saddr) {
			saddr->sipc_family = AF_RTIPC;
			saddr->sipc_port = mbuf->from;
		}
		if (maxlen >= len) {
			list_del(&mbuf->next);
			dofree = 1;
		} else {
			/* Buffer is only partially read: repost. */
			mbuf->rdoff += maxlen;
			len = maxlen;
			dofree = 0;
			rtdm_sem_up(&sk->insem);
		}
	);

	/* Now, write "len" bytes from mbuf->data to the vector cells */
	for (nvec = 0, wrlen = len; nvec < iovlen && wrlen > 0; nvec++) {
		if (iov[nvec].iov_len == 0)
			continue;
		vlen = wrlen >= iov[nvec].iov_len ? iov[nvec].iov_len : wrlen;
#ifdef CONFIG_XENO_OPT_PERVASIVE
		if (user_info) {
			xnbufd_map_uread(&bufd, iov[nvec].iov_base, vlen);
			ret = xnbufd_copy_from_kmem(&bufd, mbuf->data + rdoff, vlen);
			xnbufd_unmap_uread(&bufd);
		} else
#endif
		{
			xnbufd_map_kread(&bufd, iov[nvec].iov_base, vlen);
			ret = xnbufd_copy_from_kmem(&bufd, mbuf->data + rdoff, vlen);
			xnbufd_unmap_kread(&bufd);
		}
		if (ret < 0)
			break;
		iov[nvec].iov_base += vlen;
		iov[nvec].iov_len -= vlen;
		wrlen -= vlen;
		rdoff += vlen;
	}

	if (dofree)
		__iddp_free_mbuf(sk, mbuf);

	return ret ?: len;
}

static ssize_t iddp_recvmsg(struct rtipc_private *priv,
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

	ret = __iddp_recvmsg(priv, user_info,
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

static ssize_t iddp_read(struct rtipc_private *priv,
			 rtdm_user_info_t *user_info,
			 void *buf, size_t len)
{
	struct iovec iov = { .iov_base = buf, .iov_len = len };
	return __iddp_recvmsg(priv, user_info, &iov, 1, 0, NULL);
}

static ssize_t __iddp_sendmsg(struct rtipc_private *priv,
			      rtdm_user_info_t *user_info,
			      struct iovec *iov, int iovlen, int flags,
			      const struct sockaddr_ipc *daddr)
{
	struct iddp_socket *sk = priv->state, *rsk;
	struct rtdm_dev_context *rcontext;
	struct iddp_message *mbuf;
	ssize_t len, rdlen, vlen;
	int nvec, wroff, ret;
	struct xnbufd bufd;
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
	if (!test_bit(_IDDP_BOUND, &rsk->status)) {
		rtdm_context_unlock(rcontext);
		return -ECONNREFUSED;
	}

	mbuf = __iddp_alloc_mbuf(rsk, len, flags, sk->tx_timeout, &ret);
	if (unlikely(ret)) {
		rtdm_context_unlock(rcontext);
		return ret;
	}

	/* Now, move "len" bytes to mbuf->data from the vector cells */
	for (nvec = 0, rdlen = len, wroff = 0;
	     nvec < iovlen && rdlen > 0; nvec++) {
		if (iov[nvec].iov_len == 0)
			continue;
		vlen = rdlen >= iov[nvec].iov_len ? iov[nvec].iov_len : rdlen;
#ifdef CONFIG_XENO_OPT_PERVASIVE
		if (user_info) {
			xnbufd_map_uread(&bufd, iov[nvec].iov_base, vlen);
			ret = xnbufd_copy_to_kmem(mbuf->data + wroff, &bufd, vlen);
			xnbufd_unmap_uread(&bufd);
		} else
#endif
		{
			xnbufd_map_kread(&bufd, iov[nvec].iov_base, vlen);
			ret = xnbufd_copy_to_kmem(mbuf->data + wroff, &bufd, vlen);
			xnbufd_unmap_kread(&bufd);
		}
		if (ret < 0)
			goto fail;
		iov[nvec].iov_base += vlen;
		iov[nvec].iov_len -= vlen;
		rdlen -= vlen;
		wroff += vlen;
	}

	RTDM_EXECUTE_ATOMICALLY(
		mbuf->from = sk->name.sipc_port;
		if (flags & MSG_OOB)
			list_add(&mbuf->next, &rsk->inq);
		else
			list_add_tail(&mbuf->next, &rsk->inq);
		rtdm_sem_up(&rsk->insem);
	);

	rtdm_context_unlock(rcontext);

	return len;

fail:
	__iddp_free_mbuf(rsk, mbuf);

	rtdm_context_unlock(rcontext);

	return ret;
}

static ssize_t iddp_sendmsg(struct rtipc_private *priv,
			    rtdm_user_info_t *user_info,
			    const struct msghdr *msg, int flags)
{
	struct iddp_socket *sk = priv->state;
	struct iovec iov[RTIPC_IOV_MAX];
	struct sockaddr_ipc daddr;
	ssize_t ret;

	if (flags & ~(MSG_OOB | MSG_DONTWAIT))
		return -EINVAL;

	if (msg->msg_name) {
		if (msg->msg_namelen != sizeof(struct sockaddr_ipc))
			return -EINVAL;

		/* Fetch the destination address to send to. */
		if (rtipc_get_arg(user_info, &daddr,
				  msg->msg_name, sizeof(daddr)))
			return -EFAULT;

		if (daddr.sipc_port < 0 ||
		    daddr.sipc_port >= CONFIG_XENO_OPT_IDDP_NRPORT)
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

	ret = __iddp_sendmsg(priv, user_info, iov,
			     msg->msg_iovlen, flags, &daddr);
	if (ret <= 0)
		return ret;

	/* Copy updated I/O vector back */
	if (rtipc_put_arg(user_info, msg->msg_iov, iov,
			  sizeof(iov[0]) * msg->msg_iovlen))
		return -EFAULT;

	return ret;
}

static ssize_t iddp_write(struct rtipc_private *priv,
			  rtdm_user_info_t *user_info,
			  const void *buf, size_t len)
{
	struct iovec iov = { .iov_base = (void *)buf, .iov_len = len };
	struct iddp_socket *sk = priv->state;

	if (sk->peer.sipc_port < 0)
		return -EDESTADDRREQ;

	return __iddp_sendmsg(priv, user_info, &iov, 1, 0, &sk->peer);
}

static int __iddp_bind_socket(struct rtipc_private *priv,
			      struct sockaddr_ipc *sa)
{
	struct iddp_socket *sk = priv->state;
	int ret = 0, port, fd;
	void *poolmem;
	size_t poolsz;

	if (sa->sipc_family != AF_RTIPC)
		return -EINVAL;

	if (sa->sipc_port < -1 ||
	    sa->sipc_port >= CONFIG_XENO_OPT_IDDP_NRPORT)
		return -EINVAL;

	RTDM_EXECUTE_ATOMICALLY(
		if (test_bit(_IDDP_BOUND, &sk->status) ||
		    __test_and_set_bit(_IDDP_BINDING, &sk->status))
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
	 * Allocate a local buffer pool if we were told to do so via
	 * setsockopt() before we got there.
	 */
	poolsz = sk->poolsz;
	if (poolsz > 0) {
		poolsz = xnheap_rounded_size(poolsz, XNHEAP_PAGE_SIZE);
		poolmem = xnarch_alloc_host_mem(poolsz);
		if (poolmem == NULL) {
			ret = -ENOMEM;
			goto fail;
		}

		ret = xnheap_init(&sk->privpool,
				  poolmem, poolsz, XNHEAP_PAGE_SIZE);
		if (ret) {
			xnarch_free_host_mem(poolmem, poolsz);
			goto fail;
		}
		xnheap_set_label(&sk->privpool, "ippd: %d", port);

		sk->poolevt = &sk->privevt;
		sk->poolwait = &sk->privwait;
		sk->bufpool = &sk->privpool;
	}

	sk->name = *sa;
	/* Set default destination if unset at binding time. */
	if (sk->peer.sipc_port < 0)
		sk->peer = *sa;

	if (*sk->label) {
		ret = xnregistry_enter(sk->label, sk,
				       &sk->handle, &__iddp_pnode.node);
		if (ret) {
			if (poolsz > 0)
				xnheap_destroy(&sk->privpool,
					       __iddp_flush_pool, NULL);
			goto fail;
		}
	}

	RTDM_EXECUTE_ATOMICALLY(
		__clear_bit(_IDDP_BINDING, &sk->status);
		__set_bit(_IDDP_BOUND, &sk->status);
	);

	return 0;
fail:
	xnmap_remove(portmap, port);
	clear_bit(_IDDP_BINDING, &sk->status);

	return ret;
}

static int __iddp_connect_socket(struct iddp_socket *sk,
				 struct sockaddr_ipc *sa)
{
	struct iddp_socket *rsk;
	xnhandle_t h;
	int ret;

	if (sa == NULL) {
		sa = &nullsa;
		goto set_assoc;
	}

	if (sa->sipc_family != AF_RTIPC)
		return -EINVAL;

	if (sa->sipc_port < -1 ||
	    sa->sipc_port >= CONFIG_XENO_OPT_IDDP_NRPORT)
		return -EINVAL;
	/*
	 * - If a valid sipc_port is passed in the [0..NRPORT-1] range,
	 * it is used verbatim and the connection succeeds
	 * immediately, regardless of whether the destination is
	 * bound at the time of the call.
	 *
	 * - If sipc_port is -1 and a label was set via IDDP_LABEL,
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
			if (rsk == NULL || rsk->magic != IDDP_SOCKET_MAGIC)
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
		if (!test_bit(_IDDP_BOUND, &sk->status))
			/* Set default name. */
			sk->name = *sa;
		/* Set default destination. */
		sk->peer = *sa;
	);

	return 0;
}

static int __iddp_setsockopt(struct iddp_socket *sk,
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

	if (sopt.level != SOL_IDDP)
		return -ENOPROTOOPT;

	switch (sopt.optname) {

	case IDDP_POOLSZ:
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
			if (test_bit(_IDDP_BOUND, &sk->status) ||
			    test_bit(_IDDP_BINDING, &sk->status))
				ret = -EALREADY;
			else
				sk->poolsz = len;
		);
		break;

	case IDDP_LABEL:
		if (sopt.optlen < sizeof(plabel))
			return -EINVAL;
		if (rtipc_get_arg(user_info, &plabel,
				  sopt.optval, sizeof(plabel)))
			return -EFAULT;
		RTDM_EXECUTE_ATOMICALLY(
			/*
			 * We may attach a label to a client socket
			 * which was previously bound in IDDP.
			 */
			if (test_bit(_IDDP_BINDING, &sk->status))
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

static int __iddp_getsockopt(struct iddp_socket *sk,
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

	if (sopt.level != SOL_IDDP)
		return -ENOPROTOOPT;

	switch (sopt.optname) {

	case IDDP_LABEL:
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

static int __iddp_ioctl(struct rtipc_private *priv,
			rtdm_user_info_t *user_info,
			unsigned int request, void *arg)
{
	struct sockaddr_ipc saddr, *saddrp = &saddr;
	struct iddp_socket *sk = priv->state;
	int ret = 0;

	switch (request) {

	case _RTIOC_CONNECT:
		ret = rtipc_get_sockaddr(user_info, arg, &saddrp);
		if (ret)
		  return ret;
		ret = __iddp_connect_socket(sk, saddrp);
		break;

	case _RTIOC_BIND:
		ret = rtipc_get_sockaddr(user_info, arg, &saddrp);
		if (ret)
			return ret;
		if (saddrp == NULL)
			return -EFAULT;
		ret = __iddp_bind_socket(priv, saddrp);
		break;

	case _RTIOC_GETSOCKNAME:
		ret = rtipc_put_sockaddr(user_info, arg, &sk->name);
		break;

	case _RTIOC_GETPEERNAME:
		ret = rtipc_put_sockaddr(user_info, arg, &sk->peer);
		break;

	case _RTIOC_SETSOCKOPT:
		ret = __iddp_setsockopt(sk, user_info, arg);
		break;

	case _RTIOC_GETSOCKOPT:
		ret = __iddp_getsockopt(sk, user_info, arg);
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

static int iddp_ioctl(struct rtipc_private *priv,
		      rtdm_user_info_t *user_info,
		      unsigned int request, void *arg)
{
	if (rtdm_in_rt_context() && request == _RTIOC_BIND)
		return -ENOSYS;	/* Try downgrading to NRT */

	return __iddp_ioctl(priv, user_info, request, arg);
}

static int iddp_init(void)
{
	portmap = xnmap_create(CONFIG_XENO_OPT_IDDP_NRPORT, 0, 0);
	if (portmap == NULL)
		return -ENOMEM;

	rtdm_event_init(&poolevt, 0);

	return 0;
}

static void iddp_exit(void)
{
	rtdm_event_destroy(&poolevt);
	xnmap_delete(portmap);
}

struct rtipc_protocol iddp_proto_driver = {
	.proto_name = "iddp",
	.proto_statesz = sizeof(struct iddp_socket),
	.proto_init = iddp_init,
	.proto_exit = iddp_exit,
	.proto_ops = {
		.socket = iddp_socket,
		.close = iddp_close,
		.recvmsg = iddp_recvmsg,
		.sendmsg = iddp_sendmsg,
		.read = iddp_read,
		.write = iddp_write,
		.ioctl = iddp_ioctl,
	}
};
