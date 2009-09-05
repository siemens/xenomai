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

	rtdm_sem_t insem;
	struct list_head inq;

	nanosecs_rel_t rx_timeout;
	nanosecs_rel_t tx_timeout;
	unsigned long stalls;	/* Buffer stall counter. */
};

static struct sockaddr_ipc nullsa = { .sipc_port = -1 };

static struct iddp_socket *portmap[CONFIG_XENO_OPT_IDDP_NRPORT];

static struct xnheap msgpool;

static rtdm_event_t poolevt;

static int poolwait;

static void *poolmem;

static unsigned long poolsz = CONFIG_XENO_OPT_IDDP_POOLSZ;
module_param_named(poolsz, poolsz, ulong, 0444);
MODULE_PARM_DESC(poolsz, "Size of the IDDP message pool (in Kbytes)");

#define MAX_IOV_NUMBER  64

static inline void __iddp_init_mbuf(struct iddp_message *mbuf, size_t len)
{
	mbuf->rdoff = 0;
	mbuf->len = len;
	INIT_LIST_HEAD(&mbuf->next);
}

static struct iddp_message *__iddp_alloc_mbuf(struct iddp_socket *sk,
					      size_t len, int flags, int *pret)
{
	struct iddp_message *mbuf = NULL;
	rtdm_toseq_t timeout_seq;
	int ret = 0;

	rtdm_toseq_init(&timeout_seq, sk->tx_timeout);

	for (;;) {
		mbuf = xnheap_alloc(&msgpool, len + sizeof(*mbuf));
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
			++poolwait;
			ret = rtdm_event_timedwait(&poolevt, sk->tx_timeout,
						   &timeout_seq);
			poolwait--;
			if (unlikely(ret == -EIDRM))
				ret = -ECONNRESET;
		);
		if (ret)
			break;
	}

	*pret = ret;

	return mbuf;
}

static void __iddp_free_mbuf(struct iddp_message *mbuf)
{
	xnheap_free(&msgpool, mbuf);
	RTDM_EXECUTE_ATOMICALLY(
		/* Wake up sleepers if any. */
		if (poolwait > 0)
			rtdm_event_pulse(&poolevt);
	);
}

static int iddp_socket(struct rtipc_private *priv,
		       rtdm_user_info_t *user_info)
{
	struct iddp_socket *sk = priv->state;

	rtdm_sem_init(&sk->insem, 0);
	sk->name = nullsa;	/* Unbound */
	sk->peer = nullsa;
	sk->rx_timeout = RTDM_TIMEOUT_INFINITE;
	sk->tx_timeout = RTDM_TIMEOUT_INFINITE;
	sk->stalls = 0;
	INIT_LIST_HEAD(&sk->inq);

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
		list_splice(&sk->inq, &head);
	);

	while (!list_empty(&head)) {
		mbuf = list_entry(head.next, struct iddp_message, next);
		list_del(&mbuf->next);
		__iddp_free_mbuf(mbuf);
	}

	rtdm_sem_destroy(&sk->insem);

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
		__iddp_free_mbuf(mbuf);

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

	mbuf = __iddp_alloc_mbuf(sk, len, flags, &ret);
	if (unlikely(ret))
		return ret;

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

	to = daddr->sipc_port;

	RTDM_EXECUTE_ATOMICALLY(
		rsk = portmap[to];
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
		__iddp_free_mbuf(mbuf);
		return ret;
	}

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
	if (sa->sipc_family != AF_RTIPC)
		return -EINVAL;

	if (sa->sipc_port < -1 ||
	    sa->sipc_port >= CONFIG_XENO_OPT_IDDP_NRPORT)
		return -EINVAL;

	RTDM_EXECUTE_ATOMICALLY(
		if (sk->name.sipc_port >= 0) /* Clear previous binding. */
			portmap[sk->name.sipc_port] = NULL;
		if (sa->sipc_port >= 0) /* Set next binding. */
			portmap[sa->sipc_port] = sk;
		sk->name = *sa;
	);

	return 0;
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

	ret = __iddp_bind_socket(sk, sa); /* Set listening port. */
	if (ret)
		return ret;
set_assoc:
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
				  const struct sockaddr_ipc *sockaddr)
{
	struct _rtdm_getsockaddr_args getaddr;
	socklen_t len;

	if (rtipc_get_arg(user_info,
			  &getaddr, arg, sizeof(getaddr)))
		return -EFAULT;

	if (rtipc_get_arg(user_info,
			  &len, getaddr.addrlen, sizeof(len)))
		return -EFAULT;

	if (len < sizeof(*sockaddr))
		return -EINVAL;

	if (rtipc_put_arg(user_info,
			  getaddr.addr, sockaddr, sizeof(*sockaddr)))
		return -EFAULT;

	len = sizeof(*sockaddr);
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
	nanosecs_rel_t timeout;
	int ret = 0;

	if (rtipc_get_arg(user_info, &sopt, arg, sizeof(sopt)))
		return -EFAULT;

	if (sopt.level != SOL_RTIPC)
		return -ENOPROTOOPT;

	switch (sopt.optname) {

	case IDDP_SETRXTIMEOUT:
		if (sopt.optlen != sizeof(timeout))
			return -EINVAL;
		if (rtipc_get_arg(user_info, &timeout,
				  sopt.optval, sizeof(timeout)))
			return -EFAULT;
		sk->rx_timeout = timeout;
		break;

	case IDDP_SETTXTIMEOUT:
		if (sopt.optlen != sizeof(timeout))
			return -EINVAL;
		if (rtipc_get_arg(user_info, &timeout,
				  sopt.optval, sizeof(timeout)))
			return -EFAULT;
		sk->tx_timeout = timeout;
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

static int iddp_ioctl(struct rtipc_private *priv,
		      rtdm_user_info_t *user_info,
		      unsigned int request, void *arg)
{
	struct sockaddr_ipc saddr, *saddrp = &saddr;
	struct iddp_socket *sk = priv->state;
	int ret = 0;

	switch (request) {
	
	case _RTIOC_CONNECT:
		ret = __iddp_getuser_address(user_info, arg, &saddrp);
		if (ret == 0)
			ret = __iddp_connect_socket(sk, saddrp);
		break;

	case _RTIOC_BIND:
		ret = __iddp_getuser_address(user_info, arg, &saddrp);
		if (ret)
			return ret;
		if (saddrp == NULL)
			return -EFAULT;

		ret = __iddp_bind_socket(sk, saddrp);
		break;

	case _RTIOC_GETSOCKNAME:
		ret = __iddp_putuser_address(user_info, arg, &sk->name);
		break;

	case _RTIOC_GETPEERNAME:
		ret = __iddp_putuser_address(user_info, arg, &sk->peer);
		break;

	case _RTIOC_SETSOCKOPT:
		ret = __iddp_setsockopt(sk, user_info, arg);
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

static int __init iddp_init(void)
{
	int ret = -ENOMEM;

	if (poolsz < 4) {
		printk(KERN_ERR "invalid pool size (min 4k)\n");
		goto failed;
	}

	poolmem = vmalloc(poolsz * 1024);
	if (poolmem == NULL) {
		printk(KERN_ERR
		       "vmalloc(%lu) failed for message pool\n",
		       poolsz * 1024);
		goto failed;
	}

	ret = xnheap_init(&msgpool, poolmem, poolsz * 1024, 512);
	if (ret) {
		printk(KERN_ERR
		       "xnheap_create() failed for poolsz = %luK\n",
		       poolsz);
		goto cleanup_poolmem;
	}

	rtdm_event_init(&poolevt, 0);

	return 0;

cleanup_poolmem:
	vfree(poolmem);

failed:
	return ret;
}

struct rtipc_protocol iddp_proto_driver = {
	.proto_name = "iddp",
	.proto_statesz = sizeof(struct iddp_socket),
	.proto_init = iddp_init,
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
