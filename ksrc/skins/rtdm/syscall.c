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

static int sys_rtdm_fdcount(struct pt_regs *regs)
{
	return RTDM_FD_MAX;
}

static int sys_rtdm_open(struct pt_regs *regs)
{
	char krnl_path[RTDM_MAX_DEVNAME_LEN + 1];
	struct task_struct *p = current;

	if (unlikely(__xn_safe_strncpy_from_user(krnl_path,
						 (const char __user *)
						 __xn_reg_arg1(regs),
						 sizeof(krnl_path) - 1) < 0))
		return -EFAULT;
	krnl_path[sizeof(krnl_path) - 1] = '\0';

	return __rt_dev_open(p, (const char *)krnl_path, __xn_reg_arg2(regs));
}

static int sys_rtdm_socket(struct pt_regs *regs)
{
	return __rt_dev_socket(current, __xn_reg_arg1(regs),
			       __xn_reg_arg2(regs), __xn_reg_arg3(regs));
}

static int sys_rtdm_close(struct pt_regs *regs)
{
	return __rt_dev_close(current, __xn_reg_arg1(regs));
}

static int sys_rtdm_ioctl(struct pt_regs *regs)
{
	return __rt_dev_ioctl(current, __xn_reg_arg1(regs), __xn_reg_arg2(regs),
			      (void *)__xn_reg_arg3(regs));
}

static int sys_rtdm_read(struct pt_regs *regs)
{
	return __rt_dev_read(current, __xn_reg_arg1(regs),
			     (void *)__xn_reg_arg2(regs), __xn_reg_arg3(regs));
}

static int sys_rtdm_write(struct pt_regs *regs)
{
	return __rt_dev_write(current, __xn_reg_arg1(regs),
			      (const void *)__xn_reg_arg2(regs),
			      __xn_reg_arg3(regs));
}

static int sys_rtdm_recvmsg(struct pt_regs *regs)
{
	struct task_struct *p = current;
	struct msghdr krnl_msg;
	int ret;

	if (unlikely(!access_wok(__xn_reg_arg2(regs),
				 sizeof(krnl_msg)) ||
		     __xn_copy_from_user(&krnl_msg,
					 (void __user *)__xn_reg_arg2(regs),
					 sizeof(krnl_msg))))
		return -EFAULT;

	ret = __rt_dev_recvmsg(p, __xn_reg_arg1(regs), &krnl_msg,
			       __xn_reg_arg3(regs));
	if (unlikely(ret < 0))
		return ret;

	if (unlikely(__xn_copy_to_user((void __user *)__xn_reg_arg2(regs),
				       &krnl_msg, sizeof(krnl_msg))))
		return -EFAULT;

	return ret;
}

static int sys_rtdm_sendmsg(struct pt_regs *regs)
{
	struct task_struct *p = current;
	struct msghdr krnl_msg;

	if (unlikely(!access_rok(__xn_reg_arg2(regs),
				 sizeof(krnl_msg)) ||
		     __xn_copy_from_user(&krnl_msg,
					 (void __user *)__xn_reg_arg2(regs),
					 sizeof(krnl_msg))))
		return -EFAULT;

	return __rt_dev_sendmsg(p, __xn_reg_arg1(regs), &krnl_msg,
				__xn_reg_arg3(regs));
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

static xnsysent_t __systab[] = {
	[__rtdm_fdcount] = {sys_rtdm_fdcount, __xn_exec_any},
	[__rtdm_open] = {sys_rtdm_open, __xn_exec_current | __xn_exec_adaptive},
	[__rtdm_socket] =
	    {sys_rtdm_socket, __xn_exec_current | __xn_exec_adaptive},
	[__rtdm_close] =
	    {sys_rtdm_close, __xn_exec_current | __xn_exec_adaptive},
	[__rtdm_ioctl] =
	    {sys_rtdm_ioctl, __xn_exec_current | __xn_exec_adaptive},
	[__rtdm_read] = {sys_rtdm_read, __xn_exec_current | __xn_exec_adaptive},
	[__rtdm_write] =
	    {sys_rtdm_write, __xn_exec_current | __xn_exec_adaptive},
	[__rtdm_recvmsg] =
	    {sys_rtdm_recvmsg, __xn_exec_current | __xn_exec_adaptive},
	[__rtdm_sendmsg] =
	    {sys_rtdm_sendmsg, __xn_exec_current | __xn_exec_adaptive},
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
