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
#include <linux/string.h>
#include <nucleus/heap.h>
#include <nucleus/bufd.h>
#include <nucleus/pipe.h>
#include <rtdm/rtipc.h>
#include "internal.h"

#define XDDP_SOCKET_MAGIC 0xa21a21a2

struct xddp_message {
	struct xnpipe_mh mh;
	char data[];
};

struct xddp_socket {
	int magic;
	struct sockaddr_ipc name;
	struct sockaddr_ipc peer;

	int minor;
	size_t poolsz;
	xnhandle_t handle;
	char label[XNOBJECT_NAME_LEN];
	int fd;			/* i.e. RTDM socket fd */

	struct xddp_message *buffer;
	int buffer_port;
	xnheap_t *bufpool;
	xnheap_t privpool;
	size_t fillsz;
	size_t curbufsz;	/* Current streaming buffer size */
	u_long status;
	rtdm_lock_t lock;

	nanosecs_rel_t timeout;	/* connect()/recvmsg() timeout */
	size_t reqbufsz;	/* Requested streaming buffer size */

	int (*monitor)(int s, int event, long arg);
	struct rtipc_private *priv;
};

static struct sockaddr_ipc nullsa = {
	.sipc_family = AF_RTIPC,
	.sipc_port = -1
};

static int portmap[CONFIG_XENO_OPT_PIPE_NRDEV]; /* indexes RTDM fildes */

#define _XDDP_SYNCWAIT  0
#define _XDDP_ATOMIC    1
#define _XDDP_BINDING   2
#define _XDDP_BOUND     3

#ifdef CONFIG_XENO_OPT_VFILE

static char *__xddp_link_target(void *obj)
{
	struct xddp_socket *sk = obj;
	char *buf;

	/* XXX: older kernels don't have kasprintf(). */
	buf = kmalloc(32, GFP_KERNEL);
	if (buf == NULL)
		return buf;

	snprintf(buf, 32, "/dev/rtp%d", sk->minor);

	return buf;
}

extern struct xnptree rtipc_ptree;

static struct xnpnode_link __xddp_pnode = {
	.node = {
		.dirname = "xddp",
		.root = &rtipc_ptree,
		.ops = &xnregistry_vlink_ops,
	},
	.target = __xddp_link_target,
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_link __xddp_pnode = {
	.node = {
		.dirname = "xddp",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

static void __xddp_flush_pool(xnheap_t *heap,
			      void *poolmem, u_long poolsz, void *cookie)
{
	xnarch_free_host_mem(poolmem, poolsz);
}

static void *__xddp_alloc_handler(size_t size, void *skarg) /* nklock free */
{
	struct xddp_socket *sk = skarg;
	void *buf;

	/* Try to allocate memory for the incoming message. */
	buf = xnheap_alloc(sk->bufpool, size);
	if (unlikely(buf == NULL)) {
		if (sk->monitor)
			sk->monitor(sk->fd, XDDP_EVTNOBUF, size);
		if (size > xnheap_max_contiguous(sk->bufpool))
			buf = (void *)-1; /* Will never succeed. */
	}

	return buf;
}

static int __xddp_resize_streambuf(struct xddp_socket *sk) /* sk->lock held */
{
	if (sk->buffer)
		xnheap_free(sk->bufpool, sk->buffer);

	if (sk->reqbufsz == 0) {
		sk->buffer = NULL;
		sk->curbufsz = 0;
		return 0;
	}

	sk->buffer = xnheap_alloc(sk->bufpool, sk->reqbufsz);
	if (sk->buffer == NULL) {
		sk->curbufsz = 0;
		return -ENOMEM;
	}

	sk->curbufsz = sk->reqbufsz;

	return 0;
}

static void __xddp_free_handler(void *buf, void *skarg) /* nklock free */
{
	struct xddp_socket *sk = skarg;
	rtdm_lockctx_t lockctx;

	if (buf != sk->buffer) {
		xnheap_free(sk->bufpool, buf);
		return;
	}

	/* Reset the streaming buffer. */

	rtdm_lock_get_irqsave(&sk->lock, lockctx);

	sk->fillsz = 0;
	sk->buffer_port = -1;
	__clear_bit(_XDDP_SYNCWAIT, &sk->status);
	__clear_bit(_XDDP_ATOMIC, &sk->status);

	/*
	 * If a XDDP_BUFSZ request is pending, resize the streaming
	 * buffer on-the-fly.
	 */
	if (unlikely(sk->curbufsz != sk->reqbufsz))
		__xddp_resize_streambuf(sk);

	rtdm_lock_put_irqrestore(&sk->lock, lockctx);
}

static void __xddp_output_handler(xnpipe_mh_t *mh, void *skarg) /* nklock held */
{
	struct xddp_socket *sk = skarg;

	if (sk->monitor)
		sk->monitor(sk->fd, XDDP_EVTOUT, xnpipe_m_size(mh));
}

static int __xddp_input_handler(xnpipe_mh_t *mh, int retval, void *skarg) /* nklock held */
{
	struct xddp_socket *sk = skarg;

	if (sk->monitor == NULL)
		return retval;

	if (retval == 0)
		/* Callee may alter the return value passed to userland. */
		retval = sk->monitor(sk->fd, XDDP_EVTIN, xnpipe_m_size(mh));
	else if (retval == -EPIPE && mh == NULL)
		sk->monitor(sk->fd, XDDP_EVTDOWN, 0);

	return retval;
}

static void __xddp_release_handler(void *skarg) /* nklock free */
{
	struct xddp_socket *sk = skarg;

	if (sk->bufpool == &sk->privpool)
		xnheap_destroy(&sk->privpool, __xddp_flush_pool, NULL);

	kfree(sk);
}

static int xddp_socket(struct rtipc_private *priv,
		       rtdm_user_info_t *user_info)
{
	struct xddp_socket *sk = priv->state;

	sk->magic = XDDP_SOCKET_MAGIC;
	sk->name = nullsa;	/* Unbound */
	sk->peer = nullsa;
	sk->minor = -1;
	sk->handle = 0;
	*sk->label = 0;
	sk->poolsz = 0;
	sk->buffer = NULL;
	sk->buffer_port = -1;
	sk->bufpool = NULL;
	sk->fillsz = 0;
	sk->status = 0;
	sk->timeout = RTDM_TIMEOUT_INFINITE;
	sk->curbufsz = 0;
	sk->reqbufsz = 0;
	sk->monitor = NULL;
	rtdm_lock_init(&sk->lock);
	sk->priv = priv;

	return 0;
}

static int xddp_close(struct rtipc_private *priv,
		      rtdm_user_info_t *user_info)
{
	struct xddp_socket *sk = priv->state;

	sk->monitor = NULL;

	if (!test_bit(_XDDP_BOUND, &sk->status))
		return 0;

	portmap[sk->name.sipc_port] = -1;

	if (sk->handle)
		xnregistry_remove(sk->handle);

	return xnpipe_disconnect(sk->minor);
}

static ssize_t __xddp_recvmsg(struct rtipc_private *priv,
			      rtdm_user_info_t *user_info,
			      struct iovec *iov, int iovlen, int flags,
			      struct sockaddr_ipc *saddr)
{
	struct xddp_message *mbuf = NULL; /* Fake GCC */
	struct xddp_socket *sk = priv->state;
	ssize_t maxlen, len, wrlen, vlen;
	nanosecs_rel_t timeout;
	struct xnpipe_mh *mh;
	int nvec, rdoff, ret;
	struct xnbufd bufd;

	if (!test_bit(_XDDP_BOUND, &sk->status))
		return -EAGAIN;

	maxlen = rtipc_get_iov_flatlen(iov, iovlen);
	if (maxlen == 0)
		return 0;

	timeout = (flags & MSG_DONTWAIT) ? RTDM_TIMEOUT_NONE : sk->timeout;
	/* Pull heading message from the input queue. */
	len = xnpipe_recv(sk->minor, &mh, timeout);
	if (len < 0)
		return len == -EIDRM ? 0 : len;
	if (len > maxlen) {
		ret = -ENOBUFS;
		goto out;
	}

	mbuf = container_of(mh, struct xddp_message, mh);

	if (saddr)
		*saddr = sk->name;

	/* Write "len" bytes from mbuf->data to the vector cells */
	for (ret = 0, nvec = 0, rdoff = 0, wrlen = len;
	     nvec < iovlen && wrlen > 0; nvec++) {
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
			goto out;
		iov[nvec].iov_base += vlen;
		iov[nvec].iov_len -= vlen;
		wrlen -= vlen;
		rdoff += vlen;
	}

out:
	xnheap_free(sk->bufpool, mbuf);

	return ret ?: len;
}

static ssize_t xddp_recvmsg(struct rtipc_private *priv,
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

	ret = __xddp_recvmsg(priv, user_info,
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

static ssize_t xddp_read(struct rtipc_private *priv,
			 rtdm_user_info_t *user_info,
			 void *buf, size_t len)
{
	struct iovec iov = { .iov_base = buf, .iov_len = len };
	return __xddp_recvmsg(priv, user_info, &iov, 1, 0, NULL);
}

static ssize_t __xddp_stream(struct xddp_socket *sk,
			     int from, struct xnbufd *bufd)
{
	struct xddp_message *mbuf;
 	size_t fillptr, rembytes;
	rtdm_lockctx_t lockctx;
	ssize_t outbytes;
	int ret;

	/*
	 * xnpipe_msend() and xnpipe_mfixup() routines will only grab
	 * the nklock directly or indirectly, so holding our socket
	 * lock across those calls is fine.
	 */
	rtdm_lock_get_irqsave(&sk->lock, lockctx);

	/*
	 * There are two cases in which we must remove the cork
	 * unconditionally and send the incoming data as a standalone
	 * datagram: the destination port does not support streaming,
	 * or its streaming buffer is already filled with data issued
	 * from another port.
	 */
	if (sk->curbufsz == 0 ||
	    (sk->buffer_port >= 0 && sk->buffer_port != from)) {
		/* This will end up into a standalone datagram. */
		outbytes = 0;
		goto out;
	}

	mbuf = sk->buffer;
	rembytes = sk->curbufsz - sizeof(*mbuf) - sk->fillsz;
	outbytes = bufd->b_len > rembytes ? rembytes : bufd->b_len;
	if (likely(outbytes > 0)) {
	repeat:
		/* Mark the beginning of a should-be-atomic section. */
		__set_bit(_XDDP_ATOMIC, &sk->status);
		fillptr = sk->fillsz;
		sk->fillsz += outbytes;

		rtdm_lock_put_irqrestore(&sk->lock, lockctx);
		ret = xnbufd_copy_to_kmem(mbuf->data + fillptr,
					  bufd, outbytes);
		rtdm_lock_get_irqsave(&sk->lock, lockctx);

		if (ret < 0) {
			outbytes = ret;
			__clear_bit(_XDDP_ATOMIC, &sk->status);
			goto out;
		}

		/* We haven't been atomic, let's try again. */
		if (!__test_and_clear_bit(_XDDP_ATOMIC, &sk->status))
			goto repeat;

		if (__test_and_set_bit(_XDDP_SYNCWAIT, &sk->status))
			outbytes = xnpipe_mfixup(sk->minor,
						 &mbuf->mh, outbytes);
		else {
			sk->buffer_port = from;
			outbytes = xnpipe_send(sk->minor, &mbuf->mh,
					       outbytes + sizeof(*mbuf),
					       XNPIPE_NORMAL);
			if (outbytes > 0)
				outbytes -= sizeof(*mbuf);
		}
	}

out:
	rtdm_lock_put_irqrestore(&sk->lock, lockctx);

	return outbytes;
}

static ssize_t __xddp_sendmsg(struct rtipc_private *priv,
			      rtdm_user_info_t *user_info,
			      struct iovec *iov, int iovlen, int flags,
			      const struct sockaddr_ipc *daddr)
{
	ssize_t len, rdlen, wrlen, vlen, ret, sublen;
	struct xddp_socket *sk = priv->state;
	struct rtdm_dev_context *rcontext;
	struct xddp_message *mbuf;
	struct xddp_socket *rsk;
	int nvec, to, from;
	struct xnbufd bufd;

	len = rtipc_get_iov_flatlen(iov, iovlen);
	if (len == 0)
		return 0;

	from = sk->name.sipc_port;
	to = daddr->sipc_port;

	rcontext = rtdm_context_get(portmap[to]);
	if (rcontext == NULL)
		return -ECONNRESET;

	rsk = rtipc_context_to_state(rcontext);
	if (!test_bit(_XDDP_BOUND, &rsk->status)) {
		rtdm_context_unlock(rcontext);
		return -ECONNREFUSED;
	}

	sublen = len;
	nvec = 0;

	/*
	 * If active, the streaming buffer is already pending on the
	 * output queue, so we basically have nothing to do during a
	 * MSG_MORE -> MSG_NONE transition. Therefore, we only have to
	 * take care of filling that buffer when MSG_MORE is
	 * given. Yummie.
	 */
	if (flags & MSG_MORE) {
		for (rdlen = sublen, wrlen = 0;
		     nvec < iovlen && rdlen > 0; nvec++) {
			if (iov[nvec].iov_len == 0)
				continue;
			vlen = rdlen >= iov[nvec].iov_len ? iov[nvec].iov_len : rdlen;
#ifdef CONFIG_XENO_OPT_PERVASIVE
			if (user_info) {
				xnbufd_map_uread(&bufd, iov[nvec].iov_base, vlen);
				ret = __xddp_stream(rsk, from, &bufd);
				xnbufd_unmap_uread(&bufd);
			} else
#endif
			{
				xnbufd_map_kread(&bufd, iov[nvec].iov_base, vlen);
				ret = __xddp_stream(rsk, from, &bufd);
				xnbufd_unmap_kread(&bufd);
			}
			if (ret < 0)
				goto fail_unlock;
			wrlen += ret;
			rdlen -= ret;
			iov[nvec].iov_base += ret;
			iov[nvec].iov_len -= ret;
			/*
			 * In case of a short write to the streaming
			 * buffer, send the unsent part as a
			 * standalone datagram.
			 */
			if (ret < vlen) {
				sublen = rdlen;
				goto nostream;
			}
		}
		rtdm_context_unlock(rcontext);
		return wrlen;
	}

nostream:
	mbuf = xnheap_alloc(rsk->bufpool, sublen + sizeof(*mbuf));
	if (unlikely(mbuf == NULL)) {
		ret = -ENOMEM;
		goto fail_unlock;
	}

	/*
	 * Move "sublen" bytes to mbuf->data from the vector cells
	 */
	for (rdlen = sublen, wrlen = 0; nvec < iovlen && rdlen > 0; nvec++) {
		if (iov[nvec].iov_len == 0)
			continue;
		vlen = rdlen >= iov[nvec].iov_len ? iov[nvec].iov_len : rdlen;
#ifdef CONFIG_XENO_OPT_PERVASIVE
		if (user_info) {
			xnbufd_map_uread(&bufd, iov[nvec].iov_base, vlen);
			ret = xnbufd_copy_to_kmem(mbuf->data + wrlen, &bufd, vlen);
			xnbufd_unmap_uread(&bufd);
		} else
#endif
		{
			xnbufd_map_kread(&bufd, iov[nvec].iov_base, vlen);
			ret = xnbufd_copy_to_kmem(mbuf->data + wrlen, &bufd, vlen);
			xnbufd_unmap_kread(&bufd);
		}
		if (ret < 0)
			goto fail_freebuf;
		iov[nvec].iov_base += vlen;
		iov[nvec].iov_len -= vlen;
		rdlen -= vlen;
		wrlen += vlen;
	}

	ret = xnpipe_send(rsk->minor, &mbuf->mh,
			  sublen + sizeof(*mbuf),
			  (flags & MSG_OOB) ?
			  XNPIPE_URGENT : XNPIPE_NORMAL);

	if (unlikely(ret < 0)) {
	fail_freebuf:
		xnheap_free(rsk->bufpool, mbuf);
	fail_unlock:
		rtdm_context_unlock(rcontext);
		return ret;
	}

	rtdm_context_unlock(rcontext);

	return len;
}

static ssize_t xddp_sendmsg(struct rtipc_private *priv,
			    rtdm_user_info_t *user_info,
			    const struct msghdr *msg, int flags)
{
	struct xddp_socket *sk = priv->state;
	struct iovec iov[RTIPC_IOV_MAX];
	struct sockaddr_ipc daddr;
	ssize_t ret;

	/*
	 * We accept MSG_DONTWAIT, but do not care about it, since
	 * writing to the real-time endpoint of a message pipe must be
	 * a non-blocking operation.
	 */
	if (flags & ~(MSG_MORE | MSG_OOB | MSG_DONTWAIT))
		return -EINVAL;

	/*
	 * MSG_MORE and MSG_OOB are mutually exclusive in our
	 * implementation.
	 */
	if ((flags & (MSG_MORE | MSG_OOB)) == (MSG_MORE | MSG_OOB))
		return -EINVAL;

	if (msg->msg_name) {
		if (msg->msg_namelen != sizeof(struct sockaddr_ipc))
			return -EINVAL;

		/* Fetch the destination address to send to. */
		if (rtipc_get_arg(user_info, &daddr,
				  msg->msg_name, sizeof(daddr)))
			return -EFAULT;

		if (daddr.sipc_port < 0 ||
		    daddr.sipc_port >= CONFIG_XENO_OPT_PIPE_NRDEV)
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

	ret = __xddp_sendmsg(priv, user_info, iov,
			     msg->msg_iovlen, flags, &daddr);
	if (ret <= 0)
		return ret;

	/* Copy updated I/O vector back */
	if (rtipc_put_arg(user_info, msg->msg_iov, iov,
			  sizeof(iov[0]) * msg->msg_iovlen))
		return -EFAULT;

	return ret;
}

static ssize_t xddp_write(struct rtipc_private *priv,
			  rtdm_user_info_t *user_info,
			  const void *buf, size_t len)
{
	struct iovec iov = { .iov_base = (void *)buf, .iov_len = len };
	struct xddp_socket *sk = priv->state;

	if (sk->peer.sipc_port < 0)
		return -EDESTADDRREQ;

	return __xddp_sendmsg(priv, user_info, &iov, 1, 0, &sk->peer);
}

static int __xddp_bind_socket(struct rtipc_private *priv,
			      struct sockaddr_ipc *sa)
{
	struct xddp_socket *sk = priv->state;
	struct xnpipe_operations ops;
	size_t poolsz;
	void *poolmem;
	int ret = 0;

	if (sa->sipc_family != AF_RTIPC)
		return -EINVAL;

	/* Allow special port -1 for auto-selection. */
	if (sa->sipc_port < -1 ||
	    sa->sipc_port >= CONFIG_XENO_OPT_PIPE_NRDEV)
		return -EINVAL;

	RTDM_EXECUTE_ATOMICALLY(
		if (test_bit(_XDDP_BOUND, &sk->status) ||
		    __test_and_set_bit(_XDDP_BINDING, &sk->status))
			ret = -EADDRINUSE;
	);
	if (ret)
		return ret;

	poolsz = sk->poolsz;
	if (poolsz > 0) {
		poolsz = xnheap_rounded_size(poolsz + sk->reqbufsz, XNHEAP_PAGE_SIZE);
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

		sk->bufpool = &sk->privpool;
	} else
		sk->bufpool = &kheap;

	if (sk->reqbufsz > 0) {
		sk->buffer = xnheap_alloc(sk->bufpool, sk->reqbufsz);
		if (sk->buffer == NULL) {
			ret = -ENOMEM;
			goto fail_freeheap;
		}
		sk->curbufsz = sk->reqbufsz;
	}

	sk->fd = rtdm_private_to_context(priv)->fd;

	ops.output = &__xddp_output_handler;
	ops.input = &__xddp_input_handler;
	ops.alloc_ibuf = &__xddp_alloc_handler;
	ops.free_ibuf = &__xddp_free_handler;
	ops.free_obuf = &__xddp_free_handler;
	ops.release = &__xddp_release_handler;

	ret = xnpipe_connect(sa->sipc_port, &ops, sk);
	if (ret < 0) {
		if (ret == -EBUSY)
			ret = -EADDRINUSE;
	fail_freeheap:
		if (sk->bufpool == &sk->privpool)
			xnheap_destroy(&sk->privpool, __xddp_flush_pool, NULL);
	fail:
		clear_bit(_XDDP_BINDING, &sk->status);
		return ret;
	}

	sk->minor = ret;
	sa->sipc_port = ret;
	sk->name = *sa;
	/* Set default destination if unset at binding time. */
	if (sk->peer.sipc_port < 0)
		sk->peer = *sa;

	if (poolsz > 0)
		xnheap_set_label(sk->bufpool, "xddp: %d", sa->sipc_port);

	if (*sk->label) {
		ret = xnregistry_enter(sk->label, sk, &sk->handle,
				       &__xddp_pnode.node);
		if (ret) {
			/* The release handler will cleanup the pool for us. */
			xnpipe_disconnect(sk->minor);
			return ret;
		}
	}

	RTDM_EXECUTE_ATOMICALLY(
		portmap[sk->minor] = sk->fd;
		__clear_bit(_XDDP_BINDING, &sk->status);
		__set_bit(_XDDP_BOUND, &sk->status);
	);

	return 0;
}

static int __xddp_connect_socket(struct xddp_socket *sk,
				 struct sockaddr_ipc *sa)
{
	struct xddp_socket *rsk;
	xnhandle_t h;
	int ret;

	if (sa == NULL) {
		sa = &nullsa;
		goto set_assoc;
	}

	if (sa->sipc_family != AF_RTIPC)
		return -EINVAL;

	if (sa->sipc_port < -1 ||
	    sa->sipc_port >= CONFIG_XENO_OPT_PIPE_NRDEV)
		return -EINVAL;
	/*
	 * - If a valid sipc_port is passed in the [0..NRDEV-1] range,
	 * it is used verbatim and the connection succeeds
	 * immediately, regardless of whether the destination is
	 * bound at the time of the call.
	 *
	 * - If sipc_port is -1 and a label was set via XDDP_LABEL,
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
				      sk->timeout, XN_RELATIVE, &h);
		if (ret)
			return ret;

		RTDM_EXECUTE_ATOMICALLY(
			rsk = xnregistry_fetch(h);
			if (rsk == NULL || rsk->magic != XDDP_SOCKET_MAGIC)
				ret = -EINVAL;
			else
				/* Fetch labeled port number. */
				sa->sipc_port = rsk->minor;
		);
		if (ret)
			return ret;
	}

set_assoc:
	RTDM_EXECUTE_ATOMICALLY(
		if (!test_bit(_XDDP_BOUND, &sk->status))
			/* Set default name. */
			sk->name = *sa;
		/* Set default destination. */
		sk->peer = *sa;
	);

	return 0;
}

static int __xddp_setsockopt(struct xddp_socket *sk,
			     rtdm_user_info_t *user_info,
			     void *arg)
{
	int (*monitor)(int s, int event, long arg);
	struct _rtdm_setsockopt_args sopt;
	struct rtipc_port_label plabel;
	rtdm_lockctx_t lockctx;
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
			sk->timeout = rtipc_timeval_to_ns(&tv);
			break;

		default:
			ret = -EINVAL;
		}

		return ret;
	}

	if (sopt.level != SOL_XDDP)
		return -ENOPROTOOPT;

	switch (sopt.optname) {

	case XDDP_BUFSZ:
		if (sopt.optlen != sizeof(len))
			return -EINVAL;
		if (rtipc_get_arg(user_info, &len,
				  sopt.optval, sizeof(len)))
			return -EFAULT;
		if (len > 0) {
			len += sizeof(struct xddp_message);
			if (sk->bufpool &&
			    len > xnheap_max_contiguous(sk->bufpool)) {
				return -EINVAL;
			}
		}
		rtdm_lock_get_irqsave(&sk->lock, lockctx);
		sk->reqbufsz = len;
		if (len != sk->curbufsz &&
		    !test_bit(_XDDP_SYNCWAIT, &sk->status) &&
		    test_bit(_XDDP_BOUND, &sk->status))
			ret = __xddp_resize_streambuf(sk);
		rtdm_lock_put_irqrestore(&sk->lock, lockctx);
		break;

	case XDDP_POOLSZ:
		if (sopt.optlen != sizeof(len))
			return -EINVAL;
		if (rtipc_get_arg(user_info, &len,
				  sopt.optval, sizeof(len)))
			return -EFAULT;
		if (len == 0)
			return -EINVAL;
		RTDM_EXECUTE_ATOMICALLY(
			if (test_bit(_XDDP_BOUND, &sk->status) ||
			    test_bit(_XDDP_BINDING, &sk->status))
				ret = -EALREADY;
			else
				sk->poolsz = len;
		);
		break;

	case XDDP_MONITOR:
		/* Monitoring is available from kernel-space only. */
		if (user_info)
			return -EPERM;
		if (sopt.optlen != sizeof(monitor))
			return -EINVAL;
		if (rtipc_get_arg(NULL, &monitor,
				  sopt.optval, sizeof(monitor)))
			return -EFAULT;
		sk->monitor = monitor;
		break;

	case XDDP_LABEL:
		if (sopt.optlen < sizeof(plabel))
			return -EINVAL;
		if (rtipc_get_arg(user_info, &plabel,
				  sopt.optval, sizeof(plabel)))
			return -EFAULT;
		RTDM_EXECUTE_ATOMICALLY(
			if (test_bit(_XDDP_BOUND, &sk->status) ||
			    test_bit(_XDDP_BINDING, &sk->status))
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

static int __xddp_getsockopt(struct xddp_socket *sk,
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
			rtipc_ns_to_timeval(&tv, sk->timeout);
			if (rtipc_put_arg(user_info, sopt.optval,
					  &tv, sizeof(tv)))
				return -EFAULT;
			break;

		default:
			ret = -EINVAL;
		}

		return ret;
	}

	if (sopt.level != SOL_XDDP)
		return -ENOPROTOOPT;

	switch (sopt.optname) {

	case XDDP_LABEL:
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

static int __xddp_ioctl(struct rtipc_private *priv,
			rtdm_user_info_t *user_info,
			unsigned int request, void *arg)
{
	struct sockaddr_ipc saddr, *saddrp = &saddr;
	struct xddp_socket *sk = priv->state;
	int ret = 0;

	switch (request) {

	case _RTIOC_CONNECT:
		ret = rtipc_get_sockaddr(user_info, arg, &saddrp);
		if (ret == 0)
			ret = __xddp_connect_socket(sk, saddrp);
		break;

	case _RTIOC_BIND:
		ret = rtipc_get_sockaddr(user_info, arg, &saddrp);
		if (ret)
			return ret;
		if (saddrp == NULL)
			return -EFAULT;
		ret = __xddp_bind_socket(priv, saddrp);
		break;

	case _RTIOC_GETSOCKNAME:
		ret = rtipc_put_sockaddr(user_info, arg, &sk->name);
		break;

	case _RTIOC_GETPEERNAME:
		ret = rtipc_put_sockaddr(user_info, arg, &sk->peer);
		break;

	case _RTIOC_SETSOCKOPT:
		ret = __xddp_setsockopt(sk, user_info, arg);
		break;

	case _RTIOC_GETSOCKOPT:
		ret = __xddp_getsockopt(sk, user_info, arg);
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

static int xddp_ioctl(struct rtipc_private *priv,
		      rtdm_user_info_t *user_info,
		      unsigned int request, void *arg)
{
	if (rtdm_in_rt_context() && request == _RTIOC_BIND)
		return -ENOSYS;	/* Try downgrading to NRT */

	return __xddp_ioctl(priv, user_info, request, arg);
}

struct rtipc_protocol xddp_proto_driver = {
	.proto_name = "xddp",
	.proto_statesz = sizeof(struct xddp_socket),
	.proto_ops = {
		.socket = xddp_socket,
		.close = xddp_close,
		.recvmsg = xddp_recvmsg,
		.sendmsg = xddp_sendmsg,
		.read = xddp_read,
		.write = xddp_write,
		.ioctl = xddp_ioctl,
	}
};
