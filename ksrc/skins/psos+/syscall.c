/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2006 Philippe Gerum <rpm@xenomai.org> 
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

#include <nucleus/shadow.h>
#include <nucleus/registry.h>
#include <psos+/defs.h>
#include <psos+/task.h>
#include <psos+/syscall.h>

/*
 * By convention, error codes are passed back through the syscall
 * return value:
 * - negative codes stand for internal (i.e. nucleus) errors;
 * - strictly positive values stand for genuine pSOS errors.
 * - zero means success.
 *
 * NOTE: the pSOS skin normally returns object memory addresses as
 * identifiers to kernel-space users. For user-space callers, we go
 * though the registry for obtaining safe identifiers.
 */

static int __muxid;

static psostask_t *__psos_task_current(struct task_struct *curr)
{
	xnthread_t *thread = xnshadow_thread(curr);

	if (!thread || xnthread_get_magic(thread) != PSOS_SKIN_MAGIC)
		return NULL;

	return thread2psostask(thread);	/* Convert TCB pointers. */
}

/*
 * int __t_create(const char *name,
 *                u_long prio,
 *                u_long flags,
 *                u_long *tid_r,
 *                xncompletion_t *completion)
 */

static int __t_create(struct task_struct *curr, struct pt_regs *regs)
{
	xncompletion_t __user *u_completion;
	u_long prio, flags, tid, *tid_r, err;
	psostask_t *task;
	char name[4];

	if (!__xn_access_ok(curr, VERIFY_READ, __xn_reg_arg1(regs), sizeof(name)))
		return -EFAULT;

	/* Get task name. */
	__xn_strncpy_from_user(curr, name, (const char __user *)__xn_reg_arg1(regs),
			       sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	strncpy(curr->comm, name, sizeof(curr->comm));
	curr->comm[sizeof(curr->comm) - 1] = '\0';

	if (!__xn_access_ok
	    (curr, VERIFY_WRITE, __xn_reg_arg4(regs), sizeof(*tid_r)))
		return -EFAULT;

	/* Task priority. */
	prio = __xn_reg_arg2(regs);
	/* Task flags. Force FPU support in user-space. This will lead
	   to a no-op if the platform does not support it. */
	flags = __xn_reg_arg3(regs) | T_SHADOW | T_FPU;
	/* Completion descriptor our parent thread is pending on. */
	u_completion = (xncompletion_t __user *)__xn_reg_arg5(regs);

	err = t_create(name, prio, 0, 0, flags, &tid);

	if (err == SUCCESS) {
		task = (psostask_t *)tid;
		/* Copy back the registry handle. */
		tid = xnthread_handle(&task->threadbase);
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg4(regs), &tid,
				  sizeof(tid));
		err = xnshadow_map(&task->threadbase, u_completion);
	} else {
		/* Unblock and pass back error code. */

		if (u_completion)
			xnshadow_signal_completion(u_completion, err);
	}

	return err;
}

/*
 * int __t_start(u_long tid,
 *	         u_long mode,
 *	         void (*startaddr) (u_long, u_long, u_long, u_long),
 *  	         u_long targs[])
 */

static int __t_start(struct task_struct *curr, struct pt_regs *regs)
{
	void (*startaddr)(u_long, u_long, u_long, u_long);
	u_long mode, *argp;
	xnhandle_t handle;
	psostask_t *task;

	handle = __xn_reg_arg1(regs);
	task = (psostask_t *)xnregistry_fetch(handle);

	if (!task)
		return ERR_OBJID;

	mode = __xn_reg_arg2(regs);
	startaddr = (typeof(startaddr))__xn_reg_arg3(regs);
	argp = (u_long *)__xn_reg_arg4(regs);

	return t_start((u_long)task, mode, startaddr, argp);
}

/*
 * int __t_delete(u_long tid)
 */
static int __t_delete(struct task_struct *curr, struct pt_regs *regs)
{
	xnhandle_t handle;
	psostask_t *task;

	handle = __xn_reg_arg1(regs);

	if (handle)
		task = (psostask_t *)xnregistry_fetch(handle);
	else
		task = __psos_task_current(curr);

	if (!task)
		return ERR_OBJID;

	return t_delete((u_long)task);
}

static xnsysent_t __systab[] = {
	[__psos_t_create] = {&__t_create, __xn_exec_init},
	[__psos_t_start] = {&__t_start, __xn_exec_any},
	[__psos_t_delete] = {&__t_delete, __xn_exec_conforming},
};

static void __shadow_delete_hook(xnthread_t *thread)
{
	if (xnthread_get_magic(thread) == PSOS_SKIN_MAGIC &&
	    testbits(thread->status, XNSHADOW))
		xnshadow_unmap(thread);
}

int psos_syscall_init(void)
{
	__muxid =
	    xnshadow_register_interface("psos",
					PSOS_SKIN_MAGIC,
					sizeof(__systab) / sizeof(__systab[0]),
					__systab, NULL, THIS_MODULE);
	if (__muxid < 0)
		return -ENOSYS;

	xnpod_add_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);

	return 0;
}

void psos_syscall_cleanup(void)
{
	xnpod_remove_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);
	xnshadow_unregister_interface(__muxid);
}
