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
#include <rtdm/rtipc.h>
#include "internal.h"

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

int rtipc_get_arg(rtdm_user_info_t *user_info,
		  void *dst, const void *src, size_t len)
{
	if (user_info) {
		if (rtdm_safe_copy_from_user(user_info, dst, src, len))
			return -EFAULT;
	} else
		memcpy(dst, src, len);

	return 0;
}

int rtipc_put_arg(rtdm_user_info_t *user_info,
		  void *dst, const void *src, size_t len)
{
	if (user_info) {
		if (rtdm_safe_copy_to_user(user_info, dst, src, len))
			return -EFAULT;
	} else
		memcpy(dst, src, len);

	return 0;
}

int rtipc_get_sockaddr(rtdm_user_info_t *user_info,
		       const void *arg, struct sockaddr_ipc **saddrp)
{
	struct _rtdm_setsockaddr_args setaddr;

	if (rtipc_get_arg(user_info,
			  &setaddr, arg, sizeof(setaddr)))
		return -EFAULT;

	if (setaddr.addrlen > 0) {
		if (setaddr.addrlen != sizeof(**saddrp))
			return -EINVAL;

		if (rtipc_get_arg(user_info, *saddrp,
				  setaddr.addr, sizeof(**saddrp)))
			return -EFAULT;
	} else {
		if (setaddr.addr)
			return -EINVAL;
		*saddrp = NULL;
	}

	return 0;
}

int rtipc_put_sockaddr(rtdm_user_info_t *user_info, void *arg,
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

static int rtipc_socket(struct rtdm_dev_context *context,
			rtdm_user_info_t *user_info, int protocol)
{
	struct rtipc_protocol *proto;
	struct rtipc_private *p;
	int ret;

	if (protocol < 0 || protocol >= IPCPROTO_MAX)
		return -EPROTONOSUPPORT;

	if (protocol == IPCPROTO_IPC)
		/* Default protocol is IDDP */
		protocol = IPCPROTO_IDDP;

	proto = protocols[protocol - 1];
	if (proto == NULL)	/* Not compiled in? */
		return -ENOPROTOOPT;

	p = rtdm_context_to_private(context);
	p->proto = proto;
	p->state = kmalloc(proto->proto_statesz, GFP_KERNEL);
	if (p->state == NULL)
		return -ENOMEM;

	ret = proto->proto_ops.socket(p, user_info);
	if (ret)
		kfree(p->state);

	return ret;
}

static int rtipc_close(struct rtdm_dev_context *context,
		       rtdm_user_info_t *user_info)
{
	struct rtipc_private *p;

	p = rtdm_context_to_private(context);
	/*
	 * CAUTION: p->state shall be released by the
	 * proto_ops.close() handler when appropriate (which may be
	 * done asynchronously later, see XDDP).
	 */
	return p->proto->proto_ops.close(p, user_info);
}

static ssize_t rtipc_recvmsg(struct rtdm_dev_context *context,
			     rtdm_user_info_t *user_info,
			     struct msghdr *msg, int flags)
{
	struct rtipc_private *p = rtdm_context_to_private(context);
	return p->proto->proto_ops.recvmsg(p, user_info, msg, flags);
}

static ssize_t rtipc_sendmsg(struct rtdm_dev_context *context,
			     rtdm_user_info_t *user_info,
			     const struct msghdr *msg, int flags)
{
	struct rtipc_private *p = rtdm_context_to_private(context);
	return p->proto->proto_ops.sendmsg(p, user_info, msg, flags);
}

static ssize_t rtipc_read(struct rtdm_dev_context *context,
			  rtdm_user_info_t *user_info,
			  void *buf, size_t len)
{
	struct rtipc_private *p = rtdm_context_to_private(context);
	return p->proto->proto_ops.read(p, user_info, buf, len);
}

static ssize_t rtipc_write(struct rtdm_dev_context *context,
			   rtdm_user_info_t *user_info,
			   const void *buf, size_t len)
{
	struct rtipc_private *p = rtdm_context_to_private(context);
	return p->proto->proto_ops.write(p, user_info, buf, len);
}

static int rtipc_ioctl(struct rtdm_dev_context *context,
		       rtdm_user_info_t *user_info,
		       unsigned int request, void *arg)
{
	struct rtipc_private *p = rtdm_context_to_private(context);
	return p->proto->proto_ops.ioctl(p, user_info, request, arg);
}

static struct rtdm_device device = {
	.struct_version =	RTDM_DEVICE_STRUCT_VER,
	.device_flags	=	RTDM_PROTOCOL_DEVICE,
	.context_size	=	sizeof(struct rtipc_private),
	.device_name	=	"rtipc",
	.protocol_family=	PF_RTIPC,
	.socket_type	=	SOCK_DGRAM,
	.socket_nrt	=	rtipc_socket,
	.ops = {
		.close_nrt	=	rtipc_close,
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
	},
	.device_class		=	RTDM_CLASS_RTIPC,
	.device_sub_class	=	RTDM_SUBCLASS_GENERIC,
	.profile_version	=	1,
	.driver_name		=	"rtipc",
	.driver_version		=	RTDM_DRIVER_VER(1, 0, 0),
	.peripheral_name	=	"Real-time IPC interface",
	.proc_name		=	device.device_name,
	.provider_name		=	"Philippe Gerum (xenomai.org)",
};

int __init __rtipc_init(void)
{
	int ret, n;

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

	rtdm_dev_unregister(&device, 1000);

	for (n = 0; n < IPCPROTO_MAX; n++) {
		if (protocols[n] && protocols[n]->proto_exit)
			protocols[n]->proto_exit();
	}
}

module_init(__rtipc_init);
module_exit(__rtipc_exit);

MODULE_LICENSE("GPL");
