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
#include <rtdm/rtipc.h>
#include "internal.h"

#define trace(m,a...) printk(KERN_WARNING "%s: " m "\n", __FUNCTION__, ##a)

struct iddp_message {
	struct list_head next;
	int from;
	size_t rdoff;
	size_t len;
	char data[];
};

struct iddp_socket {
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

	nanosecs_rel_t rx_timeout;
	nanosecs_rel_t tx_timeout;
	unsigned long stalls;	/* Buffer stall counter. */

	struct rtipc_private *priv;
};

static struct sockaddr_ipc nullsa = {
	.sipc_family = AF_RTIPC,
	.sipc_port = -1
};

static struct iddp_socket *portmap[CONFIG_XENO_OPT_IDDP_NRPORT];

static rtdm_event_t poolevt;

static int poolwait;

#define MAX_IOV_NUMBER  64

#define _IDDP_BINDING  0

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
			++sk->poolwait;
			ret = rtdm_event_timedwait(sk->poolevt,
						   timeout,
						   &timeout_seq);
			sk->poolwait--;
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

	rtdm_sem_init(&sk->insem, 0);
	sk->name = nullsa;	/* Unbound */
	sk->peer = nullsa;
	sk->bufpool = &kheap;
	sk->poolevt = &poolevt;
	sk->poolwait = &poolwait;
	sk->poolsz = 0;
	sk->status = 0;
	sk->rx_timeout = RTDM_TIMEOUT_INFINITE;
	sk->tx_timeout = RTDM_TIMEOUT_INFINITE;
	sk->stalls = 0;
	INIT_LIST_HEAD(&sk->inq);
	sk->priv = priv;

	return 0;
}

static int iddp_close(struct rtipc_private *priv,
		      rtdm_user_info_t *user_info)
{
	struct iddp_socket *sk = priv->state;
	struct iddp_message *mbuf;
	LIST_HEAD(head);

	RTDM_EXECUTE_ATOMICALLY(
		if (sk->name.sipc_port > -1)
			portmap[sk->name.sipc_port] = NULL;
	);

	rtdm_sem_destroy(&sk->insem);

	if (sk->bufpool != &kheap) {
		xnheap_destroy(&sk->privpool, __iddp_flush_pool, NULL);
		return 0;
	}

	/* Send unread datagrams back to the system heap. */
	list_splice(&sk->inq, &head);
	while (!list_empty(&head)) {
		mbuf = list_entry(head.next, struct iddp_message, next);
		list_del(&mbuf->next);
		__iddp_free_mbuf(sk, mbuf);
	}

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

	/* Compute available iovec space to maxlen. */
	for (maxlen = 0, nvec = 0; nvec < iovlen; nvec++) {
		ssize_t l = iov[nvec].iov_len;
		if (l < 0 || maxlen + l < maxlen) /* SuS wants this. */
			return -EINVAL;
		maxlen += l;
	}

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
	for (nvec = 0, wrlen = len; wrlen > 0; nvec++) {
		if (iov[nvec].iov_len == 0)
			continue;
		vlen = wrlen >= iov[nvec].iov_len ? iov[nvec].iov_len : wrlen;
		ret = rtdm_safe_copy_to_user(user_info, iov[nvec].iov_base,
					     mbuf->data + rdoff, vlen);
		if (ret)
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
	struct iovec iov[MAX_IOV_NUMBER];
	struct sockaddr_ipc saddr;
	ssize_t ret;

	if (flags & ~MSG_DONTWAIT)
		return -EINVAL;

	if (msg->msg_name) {
		if (msg->msg_namelen < sizeof(struct sockaddr_ipc))
			return -EINVAL;
	} else if (msg->msg_namelen != 0)
		return -EINVAL;

	if (msg->msg_iovlen >= MAX_IOV_NUMBER)
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
	struct rtdm_dev_context *rcontext = NULL; /* Fake GCC */
	struct iddp_message *mbuf;
	int nvec, wroff, ret, to;
	ssize_t len, rdlen, vlen;

	/* Compute the required buffer space. */
	for (len = 0, nvec = 0; nvec < iovlen; nvec++) {
		ssize_t l = iov[nvec].iov_len;
		if (l < 0 || len + l < len) /* SuS wants this. */
			return -EINVAL;
		len += l;
	}

	if (len == 0)
		return 0;

	to = daddr->sipc_port;

	RTDM_EXECUTE_ATOMICALLY(
		rsk = portmap[to];
		if (unlikely(rsk == NULL))
			ret = -ECONNRESET;
		else {
			rcontext = rtdm_private_to_context(rsk->priv);
			rtdm_context_lock(rcontext);
			ret = 0;
		}
	);
	if (ret)
		return ret;

	mbuf = __iddp_alloc_mbuf(rsk, len, flags, sk->tx_timeout, &ret);
	if (unlikely(ret)) {
		rtdm_context_unlock(rcontext);
		return ret;
	}

	/* Now, move "len" bytes to mbuf->data from the vector cells */
	for (nvec = 0, rdlen = len, wroff = 0; rdlen > 0; nvec++) {
		if (iov[nvec].iov_len == 0)
			continue;
		vlen = rdlen >= iov[nvec].iov_len ? iov[nvec].iov_len : rdlen;
		ret = rtdm_safe_copy_from_user(user_info, mbuf->data + wroff,
					       iov[nvec].iov_base, vlen);
		if (ret)
			goto fail;
		iov[nvec].iov_base += vlen;
		iov[nvec].iov_len -= vlen;
		rdlen -= vlen;
		wroff += vlen;
	}

	RTDM_EXECUTE_ATOMICALLY(
		rsk = portmap[to];
		/*
		 * IDDP ports may be unbound dynamically, and we only
		 * hold closure, so we have to test this again.
		 */
		if (unlikely(rsk == NULL))
			ret = -ECONNRESET;
		else {
			mbuf->from = sk->name.sipc_port;
			if (flags & MSG_OOB)
				list_add(&mbuf->next, &rsk->inq);
			else
				list_add_tail(&mbuf->next, &rsk->inq);
			rtdm_sem_up(&rsk->insem);
		}
	);
	if (unlikely(ret)) {
	fail:
		__iddp_free_mbuf(rsk, mbuf);
		rtdm_context_unlock(rcontext);
		return ret;
	}

	rtdm_context_unlock(rcontext);

	return len;
}

static ssize_t iddp_sendmsg(struct rtipc_private *priv,
			    rtdm_user_info_t *user_info,
			    const struct msghdr *msg, int flags)
{
	struct iddp_socket *sk = priv->state;
	struct iovec iov[MAX_IOV_NUMBER];
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

	if (msg->msg_iovlen >= MAX_IOV_NUMBER)
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

static int __iddp_bind_socket(struct iddp_socket *sk,
			      struct sockaddr_ipc *sa)
{
	void *poolmem;
	size_t poolsz;
	int ret = 0;

	if (sa == NULL) {
		sa = &nullsa;
		goto set_binding;
	}

	if (sa->sipc_family != AF_RTIPC)
		return -EINVAL;

	if (sa->sipc_port < -1 ||
	    sa->sipc_port >= CONFIG_XENO_OPT_IDDP_NRPORT)
		return -EINVAL;

	if (test_and_set_bit(_IDDP_BINDING, &sk->status))
		return -EINPROGRESS;

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
			goto out;
		}

		ret = xnheap_init(&sk->privpool,
				  poolmem, poolsz, XNHEAP_PAGE_SIZE);
		if (ret) {
			xnarch_free_host_mem(poolmem, poolsz);
			goto out;
		}

		RTDM_EXECUTE_ATOMICALLY(
			sk->poolevt = &sk->privevt;
			sk->poolwait = &sk->privwait;
			sk->bufpool = &sk->privpool;
		);
	}

 set_binding:
	RTDM_EXECUTE_ATOMICALLY(
		if (sk->name.sipc_port >= 0) /* Clear previous binding. */
			portmap[sk->name.sipc_port] = NULL;
		if (sa->sipc_port >= 0) /* Set next binding. */
			portmap[sa->sipc_port] = sk;
		sk->name = *sa;
	);

out:
	clear_bit(_IDDP_BINDING, &sk->status);

	return ret;
}

static int __iddp_connect_socket(struct iddp_socket *sk,
				 struct sockaddr_ipc *sa)
{
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

set_assoc:
	ret = __iddp_bind_socket(sk, sa); /* Set listening port. */
	if (ret)
		return ret;
	RTDM_EXECUTE_ATOMICALLY(
		/* Set default destination. */
		sk->peer = *sa;
	);

	return 0;
}

static int __iddp_getuser_address(rtdm_user_info_t *user_info,
				  void *arg, struct sockaddr_ipc **sockaddrp)
{
	struct _rtdm_setsockaddr_args setaddr;

	if (rtipc_get_arg(user_info,
			  &setaddr, arg, sizeof(setaddr)))
		return -EFAULT;

	if (setaddr.addrlen > 0) {
		if (setaddr.addrlen != sizeof(**sockaddrp))
			return -EINVAL;

		if (rtipc_get_arg(user_info, *sockaddrp,
				  setaddr.addr, sizeof(**sockaddrp)))
			return -EFAULT;
	} else {
		if (setaddr.addr)
			return -EINVAL;
		*sockaddrp = NULL;
	}

	return 0;
}

static int __iddp_putuser_address(rtdm_user_info_t *user_info, void *arg,
				  const struct sockaddr_ipc *saddr)
{
	struct _rtdm_getsockaddr_args getaddr;
	socklen_t len;

	if (rtipc_get_arg(user_info,
			  &getaddr, arg, sizeof(getaddr)))
		return -EFAULT;

	if (rtipc_get_arg(user_info,
			  &len, getaddr.addrlen, sizeof(len)))
		return -EFAULT;

	if (len < sizeof(*saddr))
		return -EINVAL;

	if (rtipc_put_arg(user_info,
			  getaddr.addr, saddr, sizeof(*saddr)))
		return -EFAULT;

	len = sizeof(*saddr);
	if (rtipc_put_arg(user_info,
			  getaddr.addrlen, &len, sizeof(len)))
		return -EFAULT;

	return 0;
}

static int __iddp_setsockopt(struct iddp_socket *sk,
			     rtdm_user_info_t *user_info,
			     void *arg)
{
	struct _rtdm_setsockopt_args sopt;
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

	if (sopt.level != SOL_RTIPC)
		return -ENOPROTOOPT;

	switch (sopt.optname) {

	case IDDP_SETLOCALPOOL:
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
			if (test_bit(_IDDP_BINDING, &sk->status) ||
			    sk->bufpool != &kheap)
				ret = -EALREADY;
			else
				sk->poolsz = len;
		);
		break;

	case IDDP_GETSTALLCOUNT:
		if (rtipc_put_arg(user_info, arg,
				  &sk->stalls, sizeof(sk->stalls)))
			return -EFAULT;
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

	if (sopt.level != SOL_RTIPC)
		return -ENOPROTOOPT;

	switch (sopt.optname) {

	case IDDP_GETSTALLCOUNT:
		if (len < sizeof(sk->stalls))
			return -EINVAL;
		if (rtipc_put_arg(user_info, sopt.optval,
				  &sk->stalls, sizeof(sk->stalls)))
			return -EFAULT;
		break;


	default:
		ret = -EINVAL;
	}

	return ret;
}

static int __iddp_ioctl(struct iddp_socket *sk,
			rtdm_user_info_t *user_info,
			unsigned int request, void *arg)
{
	struct sockaddr_ipc saddr, *saddrp = &saddr;
	int ret = 0;

	switch (request) {
	
	case _RTIOC_CONNECT:
		ret = __iddp_getuser_address(user_info, arg, &saddrp);
		if (ret)
		  return ret;
		ret = __iddp_connect_socket(sk, saddrp);
		break;

	case _RTIOC_BIND:
		return -ENOSYS; /* Downgrade to NRT */

	case _RTIOC_GETSOCKNAME:
		ret = __iddp_putuser_address(user_info, arg, &sk->name);
		break;

	case _RTIOC_GETPEERNAME:
		ret = __iddp_putuser_address(user_info, arg, &sk->peer);
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
	struct iddp_socket *sk = priv->state;
	struct sockaddr_ipc saddr, *saddrp;
	int ret;

	if (rtdm_in_rt_context() || request != _RTIOC_BIND)
		return __iddp_ioctl(sk, user_info, request, arg);

	saddrp = &saddr;
	ret = __iddp_getuser_address(user_info, arg, &saddrp);
	if (ret)
		return ret;

	return __iddp_bind_socket(sk, saddrp);
}

static int __init iddp_init(void)
{
	rtdm_event_init(&poolevt, 0);

	return 0;
}

static void __exit iddp_exit(void)
{
	rtdm_event_destroy(&poolevt);
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
