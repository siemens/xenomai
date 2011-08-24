/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <nucleus/shadow.h>
#include <nucleus/ppd.h>
#include <rtdm/syscall.h>

#include "rtdm/internal.h"

int __rtdm_muxid;

static int sys_rtdm_fdcount(void)
{
	return RTDM_FD_MAX;
}

static int sys_rtdm_open(const char __user *u_path, int oflag)
{
	char krnl_path[RTDM_MAX_DEVNAME_LEN + 1];
	struct task_struct *p = current;

	if (unlikely(__xn_safe_strncpy_from_user(krnl_path, u_path,
						 sizeof(krnl_path) - 1) < 0))
		return -EFAULT;
	krnl_path[sizeof(krnl_path) - 1] = '\0';

	return __rt_dev_open(p, krnl_path, oflag);
}

static int sys_rtdm_socket(int protocol_family, int socket_type, int protocol)
{
	return __rt_dev_socket(current,
			       protocol_family, socket_type, protocol);
}

static int sys_rtdm_close(int fd)
{
	return __rt_dev_close(current, fd);
}

static int sys_rtdm_ioctl(int fd, int request, void *arglist)
{
	return __rt_dev_ioctl(current, fd, request, arglist);
}

static int sys_rtdm_read(int fd, void __user *u_buf, size_t nbytes)
{
	return __rt_dev_read(current, fd, u_buf, nbytes);
}

static int sys_rtdm_write(int fd, const void __user *u_buf, size_t nbytes)
{
	return __rt_dev_write(current, fd, u_buf, nbytes);
}

static int sys_rtdm_recvmsg(int fd, struct msghdr __user *u_msg, int flags)
{
	struct task_struct *p = current;
	struct msghdr krnl_msg;
	int ret;

	if (unlikely(!access_wok(u_msg, sizeof(krnl_msg)) ||
		     __xn_copy_from_user(&krnl_msg, u_msg,
					 sizeof(krnl_msg))))
		return -EFAULT;

	ret = __rt_dev_recvmsg(p, fd, &krnl_msg, flags);
	if (unlikely(ret < 0))
		return ret;

	if (unlikely(__xn_copy_to_user(u_msg, &krnl_msg, sizeof(krnl_msg))))
		return -EFAULT;

	return ret;
}

static int sys_rtdm_sendmsg(int fd, const struct msghdr __user *u_msg,
			    int flags)
{
	struct task_struct *p = current;
	struct msghdr krnl_msg;

	if (unlikely(!access_rok(u_msg, sizeof(krnl_msg)) ||
		     __xn_copy_from_user(&krnl_msg, u_msg,
					 sizeof(krnl_msg))))
		return -EFAULT;

	return __rt_dev_sendmsg(p, fd, &krnl_msg, flags);
}

static void *rtdm_skin_callback(int event, void *data)
{
	struct rtdm_process *process;

	switch (event) {
	case XNSHADOW_CLIENT_ATTACH:
		process = xnarch_alloc_host_mem(sizeof(*process));
		if (!process)
			return ERR_PTR(-ENOSPC);

#ifdef CONFIG_XENO_OPT_VFILE
		memcpy(process->name, current->comm, sizeof(process->name));
		process->pid = current->pid;
#endif /* CONFIG_XENO_OPT_VFILE */

		return &process->ppd;

	case XNSHADOW_CLIENT_DETACH:
		process = container_of((xnshadow_ppd_t *) data,
				       struct rtdm_process, ppd);

		cleanup_owned_contexts(process);

		xnarch_free_host_mem(process, sizeof(*process));

		break;
	}
	return NULL;
}

static struct xnsysent __systab[] = {
	SKINCALL_DEF(__rtdm_fdcount, sys_rtdm_fdcount, any),
	SKINCALL_DEF(__rtdm_open, sys_rtdm_open, probing),
	SKINCALL_DEF(__rtdm_socket, sys_rtdm_socket, probing),
	SKINCALL_DEF(__rtdm_close, sys_rtdm_close, probing),
	SKINCALL_DEF(__rtdm_ioctl, sys_rtdm_ioctl, probing),
	SKINCALL_DEF(__rtdm_read, sys_rtdm_read, probing),
	SKINCALL_DEF(__rtdm_write, sys_rtdm_write, probing),
	SKINCALL_DEF(__rtdm_recvmsg, sys_rtdm_recvmsg, probing),
	SKINCALL_DEF(__rtdm_sendmsg, sys_rtdm_sendmsg, probing),
};

static struct xnskin_props __props = {
	.name = "rtdm",
	.magic = RTDM_SKIN_MAGIC,
	.nrcalls = sizeof(__systab) / sizeof(__systab[0]),
	.systab = __systab,
	.eventcb = &rtdm_skin_callback,
	.timebasep = NULL,
	.module = THIS_MODULE
};

int __init rtdm_syscall_init(void)
{
	__rtdm_muxid = xnshadow_register_interface(&__props);

	if (__rtdm_muxid < 0)
		return -ENOSYS;

	return 0;
}
