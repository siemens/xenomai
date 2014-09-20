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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <rtdm/ipc.h>
#include "internal.h"

MODULE_DESCRIPTION("Real-time IPC interface");
MODULE_AUTHOR("Philippe Gerum <rpm@xenomai.org>");
MODULE_LICENSE("GPL");

static struct rtipc_protocol *protocols[IPCPROTO_MAX] = {
#ifdef CONFIG_XENO_DRIVERS_RTIPC_XDDP
	[IPCPROTO_XDDP - 1] = &xddp_proto_driver,
#endif
#ifdef CONFIG_XENO_DRIVERS_RTIPC_IDDP
	[IPCPROTO_IDDP - 1] = &iddp_proto_driver,
#endif
#ifdef CONFIG_XENO_DRIVERS_RTIPC_BUFP
	[IPCPROTO_BUFP - 1] = &bufp_proto_driver,
#endif
};

DEFINE_XNPTREE(rtipc_ptree, "rtipc");

int rtipc_get_arg(struct rtdm_fd *fd,
		  void *dst, const void *src, size_t len)
{
	if (rtdm_fd_is_user(fd)) {
		if (rtdm_safe_copy_from_user(fd, dst, src, len))
			return -EFAULT;
	} else
		memcpy(dst, src, len);

	return 0;
}

int rtipc_put_arg(struct rtdm_fd *fd,
		  void *dst, const void *src, size_t len)
{
	if (rtdm_fd_is_user(fd)) {
		if (rtdm_safe_copy_to_user(fd, dst, src, len))
			return -EFAULT;
	} else
		memcpy(dst, src, len);

	return 0;
}

int rtipc_get_sockaddr(struct rtdm_fd *fd,
		       const void *arg, struct sockaddr_ipc **saddrp)
{
	struct _rtdm_setsockaddr_args setaddr;

	if (rtipc_get_arg(fd,
			  &setaddr, arg, sizeof(setaddr)))
		return -EFAULT;

	if (setaddr.addrlen > 0) {
		if (setaddr.addrlen != sizeof(**saddrp))
			return -EINVAL;

		if (rtipc_get_arg(fd, *saddrp,
				  setaddr.addr, sizeof(**saddrp)))
			return -EFAULT;
	} else {
		if (setaddr.addr)
			return -EINVAL;
		*saddrp = NULL;
	}

	return 0;
}

int rtipc_put_sockaddr(struct rtdm_fd *fd, void *arg,
		       const struct sockaddr_ipc *saddr)
{
	struct _rtdm_getsockaddr_args getaddr;
	socklen_t len;

	if (rtipc_get_arg(fd,
			  &getaddr, arg, sizeof(getaddr)))
		return -EFAULT;

	if (rtipc_get_arg(fd,
			  &len, getaddr.addrlen, sizeof(len)))
		return -EFAULT;

	if (len < sizeof(*saddr))
		return -EINVAL;

	if (rtipc_put_arg(fd,
			  getaddr.addr, saddr, sizeof(*saddr)))
		return -EFAULT;

	len = sizeof(*saddr);
	if (rtipc_put_arg(fd,
			  getaddr.addrlen, &len, sizeof(len)))
		return -EFAULT;

	return 0;
}

ssize_t rtipc_get_iov_flatlen(struct iovec *iov, int iovlen)
{
	ssize_t len;
	int nvec;

	/* Return the flattened vector length. */
	for (len = 0, nvec = 0; nvec < iovlen; nvec++) {
		ssize_t l = iov[nvec].iov_len;
		if (l < 0 || len + l < len) /* SuS wants this. */
			return -EINVAL;
		len += l;
	}

	return len;
}

static int rtipc_socket(struct rtdm_fd *fd, int protocol)
{
	struct rtipc_protocol *proto;
	struct rtipc_private *priv;
	int ret;

	if (protocol < 0 || protocol >= IPCPROTO_MAX)
		return -EPROTONOSUPPORT;

	if (protocol == IPCPROTO_IPC)
		/* Default protocol is IDDP */
		protocol = IPCPROTO_IDDP;

	proto = protocols[protocol - 1];
	if (proto == NULL)	/* Not compiled in? */
		return -ENOPROTOOPT;

	priv = rtdm_fd_to_private(fd);
	priv->proto = proto;
	priv->state = kmalloc(proto->proto_statesz, GFP_KERNEL);
	if (priv->state == NULL)
		return -ENOMEM;

	xnselect_init(&priv->send_block);
	xnselect_init(&priv->recv_block);

	ret = proto->proto_ops.socket(fd);
	if (ret)
		kfree(priv->state);

	return ret;
}

static void rtipc_close(struct rtdm_fd *fd)
{
	struct rtipc_private *priv = rtdm_fd_to_private(fd);
	/*
	 * CAUTION: priv->state shall be released by the
	 * proto_ops.close() handler when appropriate (which may be
	 * done asynchronously later, see XDDP).
	 */
	priv->proto->proto_ops.close(fd);
	xnselect_destroy(&priv->recv_block);
	xnselect_destroy(&priv->send_block);
}

static ssize_t rtipc_recvmsg(struct rtdm_fd *fd,
			     struct msghdr *msg, int flags)
{
	struct rtipc_private *priv = rtdm_fd_to_private(fd);
	return priv->proto->proto_ops.recvmsg(fd, msg, flags);
}

static ssize_t rtipc_sendmsg(struct rtdm_fd *fd,
			     const struct msghdr *msg, int flags)
{
	struct rtipc_private *priv = rtdm_fd_to_private(fd);
	return priv->proto->proto_ops.sendmsg(fd, msg, flags);
}

static ssize_t rtipc_read(struct rtdm_fd *fd,
			  void *buf, size_t len)
{
	struct rtipc_private *priv = rtdm_fd_to_private(fd);
	return priv->proto->proto_ops.read(fd, buf, len);
}

static ssize_t rtipc_write(struct rtdm_fd *fd,
			   const void *buf, size_t len)
{
	struct rtipc_private *priv = rtdm_fd_to_private(fd);
	return priv->proto->proto_ops.write(fd, buf, len);
}

static int rtipc_ioctl(struct rtdm_fd *fd,
		       unsigned int request, void *arg)
{
	struct rtipc_private *priv = rtdm_fd_to_private(fd);
	return priv->proto->proto_ops.ioctl(fd, request, arg);
}

static int rtipc_select(struct rtdm_fd *fd, struct xnselector *selector,
			unsigned int type, unsigned int index)
{
	struct rtipc_private *priv = rtdm_fd_to_private(fd);
	struct xnselect_binding *binding;
	unsigned int pollstate, mask;
	struct xnselect *block;
	spl_t s;
	int ret;
	
	pollstate = priv->proto->proto_ops.pollstate(fd);

	switch (type) {
	case XNSELECT_READ:
		mask = pollstate & POLLIN;
		block = &priv->recv_block;
		break;
	case XNSELECT_WRITE:
		mask = pollstate & POLLOUT;
		block = &priv->send_block;
		break;
	default:
		return -EINVAL;
	}

	binding = xnmalloc(sizeof(*binding));
	if (binding == NULL)
		return -ENOMEM;

	xnlock_get_irqsave(&nklock, s);
	ret = xnselect_bind(block, binding, selector, type, index, mask);
	xnlock_put_irqrestore(&nklock, s);

	if (ret)
		xnfree(binding);

	return ret;
}

static struct rtdm_driver rtipc_driver = {
	.profile_info		=	RTDM_PROFILE_INFO(rtipc,
							  RTDM_CLASS_RTIPC,
							  RTDM_SUBCLASS_GENERIC,
							  1),
	.device_flags		=	RTDM_PROTOCOL_DEVICE,
	.device_count		=	1,
	.context_size		=	sizeof(struct rtipc_private),
	.protocol_family	=	PF_RTIPC,
	.socket_type		=	SOCK_DGRAM,
	.ops = {
		.socket		=	rtipc_socket,
		.close		=	rtipc_close,
		.recvmsg_rt	=	rtipc_recvmsg,
		.recvmsg_nrt	=	NULL,
		.sendmsg_rt	=	rtipc_sendmsg,
		.sendmsg_nrt	=	NULL,
		.ioctl_rt	=	rtipc_ioctl,
		.ioctl_nrt	=	rtipc_ioctl,
		.read_rt	=	rtipc_read,
		.read_nrt	=	NULL,
		.write_rt	=	rtipc_write,
		.write_nrt	=	NULL,
		.select		=	rtipc_select,
	},
};

static struct rtdm_device device = {
	.driver = &rtipc_driver,
	.label = "rtipc",
};

int __init __rtipc_init(void)
{
	int ret, n;

	if (!realtime_core_enabled())
		return 0;

	for (n = 0; n < IPCPROTO_MAX; n++) {
		if (protocols[n] && protocols[n]->proto_init) {
			ret = protocols[n]->proto_init();
			if (ret)
				return ret;
		}
	}

	return rtdm_dev_register(&device);
}

void __exit __rtipc_exit(void)
{
	int n;

	if (!realtime_core_enabled())
		return;

	rtdm_dev_unregister(&device);

	for (n = 0; n < IPCPROTO_MAX; n++) {
		if (protocols[n] && protocols[n]->proto_exit)
			protocols[n]->proto_exit();
	}
}

module_init(__rtipc_init);
module_exit(__rtipc_exit);
