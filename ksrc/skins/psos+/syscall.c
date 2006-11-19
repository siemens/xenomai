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
#include <psos+/queue.h>

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
	u_long prio, flags, tid, err;
	psostask_t *task;
	char name[5];

	if (!__xn_access_ok(curr, VERIFY_READ, __xn_reg_arg1(regs), sizeof(name)))
		return -EFAULT;

	/* Get task name. */
	__xn_strncpy_from_user(curr, name, (const char __user *)__xn_reg_arg1(regs),
			       sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	strncpy(curr->comm, name, sizeof(curr->comm));
	curr->comm[sizeof(curr->comm) - 1] = '\0';

	if (!__xn_access_ok
	    (curr, VERIFY_WRITE, __xn_reg_arg4(regs), sizeof(tid)))
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

/*
 * int __t_suspend(u_long tid)
 */

static int __t_suspend(struct task_struct *curr, struct pt_regs *regs)
{
	xnhandle_t handle = __xn_reg_arg1(regs);
	psostask_t *task;

	if (handle)
		task = (psostask_t *)xnregistry_fetch(handle);
	else
		task = __psos_task_current(curr);

	if (!task)
		return ERR_OBJID;

	return t_suspend((u_long)task);
}

/*
 * int __t_resume(u_long tid)
 */

static int __t_resume(struct task_struct *curr, struct pt_regs *regs)
{
	xnhandle_t handle = __xn_reg_arg1(regs);
	psostask_t *task;

	if (handle)
		task = (psostask_t *)xnregistry_fetch(handle);
	else
		task = __psos_task_current(curr);

	if (!task)
		return ERR_OBJID;

	return t_resume((u_long)task);
}

/*
 * int __t_ident(char name[4], u_long *tid_r)
 */

static int __t_ident(struct task_struct *curr, struct pt_regs *regs)
{
	char name[4], *namep;
	u_long err, tid;

	if (__xn_reg_arg1(regs)) {
		if (!__xn_access_ok(curr, VERIFY_READ, __xn_reg_arg1(regs), sizeof(name)))
			return -EFAULT;

		/* Get task name. */
		__xn_strncpy_from_user(curr, name, (const char __user *)__xn_reg_arg1(regs),
				       sizeof(name));
		namep = name;
	} else
		namep = NULL;

	if (!__xn_access_ok
	    (curr, VERIFY_WRITE, __xn_reg_arg2(regs), sizeof(tid)))
		return -EFAULT;

	err = t_ident(namep, 0, &tid);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg2(regs), &tid,
				  sizeof(tid));

	return err;
}

/*
 * int __t_mode(u_long clrmask, u_long setmask, u_long *oldmode_r)
 */

static int __t_mode(struct task_struct *curr, struct pt_regs *regs)
{
	u_long clrmask, setmask, oldmode, err;

	if (!__xn_access_ok
	    (curr, VERIFY_WRITE, __xn_reg_arg3(regs), sizeof(oldmode)))
		return -EFAULT;

	clrmask = __xn_reg_arg1(regs);
	setmask = __xn_reg_arg2(regs);

	err = t_mode(clrmask, setmask, &oldmode);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg3(regs), &oldmode,
				  sizeof(oldmode));

	return err;
}

/*
 * int __t_setpri(u_long tid, u_long newprio, u_long *oldprio_r)
 */

static int __t_setpri(struct task_struct *curr, struct pt_regs *regs)
{
	xnhandle_t handle = __xn_reg_arg1(regs);
	u_long err, newprio, oldprio;
	psostask_t *task;

	if (handle)
		task = (psostask_t *)xnregistry_fetch(handle);
	else
		task = __psos_task_current(curr);

	if (!task)
		return ERR_OBJID;

	if (!__xn_access_ok
	    (curr, VERIFY_WRITE, __xn_reg_arg3(regs), sizeof(oldprio)))
		return -EFAULT;

	newprio = __xn_reg_arg2(regs);

	err = t_setpri((u_long)task, newprio, &oldprio);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg3(regs), &oldprio,
				  sizeof(oldprio));

	return err;
}

/*
 * int __ev_send(u_long tid, u_long events)
 */

static int __ev_send(struct task_struct *curr, struct pt_regs *regs)
{
	xnhandle_t handle = __xn_reg_arg1(regs);
	psostask_t *task;
	u_long events;

	if (handle)
		task = (psostask_t *)xnregistry_fetch(handle);
	else
		task = __psos_task_current(curr);

	if (!task)
		return ERR_OBJID;

	events = __xn_reg_arg2(regs);

	return ev_send((u_long)task, events);
}

/*
 * int __ev_receive(u_long events, u_long flags, u_long timeout, u_long *events_r)
 */

static int __ev_receive(struct task_struct *curr, struct pt_regs *regs)
{
	u_long err, flags, timeout, events, events_r;

	if (!__xn_access_ok
	    (curr, VERIFY_WRITE, __xn_reg_arg4(regs), sizeof(events_r)))
		return -EFAULT;

	events = __xn_reg_arg1(regs);
	flags = __xn_reg_arg2(regs);
	timeout = __xn_reg_arg3(regs);

	err = ev_receive(events, flags, timeout, &events_r);

	if (!err)
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg4(regs), &events_r,
				  sizeof(events_r));

	return err;
}

/*
 * int __q_create(char name[4], u_long maxnum, u_long flags, u_long *qid)
 */

static int __q_create(struct task_struct *curr, struct pt_regs *regs)
{
	u_long maxnum, flags, qid, err;
	psosqueue_t *queue;
	char name[5];

	if (!__xn_access_ok(curr, VERIFY_READ, __xn_reg_arg1(regs), sizeof(name)))
		return -EFAULT;

	/* Get queue name. */
	__xn_strncpy_from_user(curr, name, (const char __user *)__xn_reg_arg1(regs),
			       sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';

	if (!__xn_access_ok
	    (curr, VERIFY_WRITE, __xn_reg_arg4(regs), sizeof(qid)))
		return -EFAULT;

	/* Max message number. */
	maxnum = __xn_reg_arg2(regs);
	/* Queue flags. */
	flags = __xn_reg_arg3(regs);

	err = q_create(name, maxnum, flags, &qid);

	if (err == SUCCESS) {
		queue = (psosqueue_t *)qid;
		/* Copy back the registry handle. */
		qid = queue->handle;
		__xn_copy_to_user(curr, (void __user *)__xn_reg_arg4(regs), &qid,
				  sizeof(qid));
	}

	return err;
}

/*
 * int __q_delete(u_long qid)
 */

static int __q_delete(struct task_struct *curr, struct pt_regs *regs)
{
	xnhandle_t handle = __xn_reg_arg1(regs);
	psosqueue_t *queue;

	queue = (psosqueue_t *)xnregistry_fetch(handle);

	if (!queue)
		return ERR_OBJID;

	return q_delete((u_long)queue);
}

static xnsysent_t __systab[] = {
	[__psos_t_create] = {&__t_create, __xn_exec_init},
	[__psos_t_start] = {&__t_start, __xn_exec_any},
	[__psos_t_delete] = {&__t_delete, __xn_exec_conforming},
	[__psos_t_suspend] = {&__t_suspend, __xn_exec_conforming},
	[__psos_t_resume] = {&__t_resume, __xn_exec_any},
	[__psos_t_ident] = {&__t_ident, __xn_exec_any},
	[__psos_t_mode] = {&__t_mode, __xn_exec_primary},
	[__psos_t_setpri] = {&__t_setpri, __xn_exec_conforming},
	[__psos_ev_send] = {&__ev_send, __xn_exec_any},
	[__psos_ev_receive] = {&__ev_receive, __xn_exec_primary},
	[__psos_q_create] = {&__q_create, __xn_exec_any},
	[__psos_q_delete] = {&__q_delete, __xn_exec_any},
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
