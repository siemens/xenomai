/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org>
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

#include <linux/ioport.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/bufd.h>
#include <nucleus/shadow.h>
#include <nucleus/registry.h>
#include <nucleus/sys_ppd.h>
#include <native/syscall.h>
#include <native/task.h>
#include <native/timer.h>
#include <native/sem.h>
#include <native/event.h>
#include <native/mutex.h>
#include <native/cond.h>
#include <native/queue.h>
#include <native/heap.h>
#include <native/alarm.h>
#include <native/intr.h>
#include <native/pipe.h>
#include <native/buffer.h>
#include <native/misc.h>

#define rt_task_errno (*xnthread_get_errno_location(xnpod_current_thread()))

/*
 * This file implements the Xenomai syscall wrappers.  All skin
 * services (re-)check the object descriptor they are passed; so there
 * may be no race between a call to xnregistry_fetch() where the
 * user-space handle is converted to a descriptor pointer, and the use
 * of it in the actual syscall.
 */

int __native_muxid;

static int __rt_bind_helper(struct task_struct *p,
			    struct pt_regs *regs,
			    xnhandle_t *handlep,
			    unsigned magic, void **objaddrp,
			    unsigned long objoffs)
{
	char name[XNOBJECT_NAME_LEN];
	RTIME timeout;
	void *objaddr;
	spl_t s;
	int err;

	if (__xn_safe_strncpy_from_user(name,
					(const char __user *)__xn_reg_arg2(regs),
					sizeof(name) - 1) < 0)
		return -EFAULT;

	name[sizeof(name) - 1] = '\0';

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg3(regs),
				     sizeof(timeout)))
		return -EFAULT;

	err = xnregistry_bind(name, timeout, XN_RELATIVE, handlep);

	if (err)
		return err;

	xnlock_get_irqsave(&nklock, s);

	objaddr = xnregistry_fetch(*handlep);

	/* Also validate the type of the bound object. */

	if (xeno_test_magic(objaddr + objoffs, magic)) {
		if (objaddrp)
			*objaddrp = objaddr;
	} else
		err = -EACCES;

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

static RT_TASK *__rt_task_lookup(xnhandle_t threadh)
{
	return thread2rtask(xnthread_lookup(threadh));
}

static RT_TASK *__rt_task_current(struct task_struct *p)
{
	xnthread_t *thread = xnshadow_thread(p);

	/* Don't call rt_task_self() which does not know about relaxed
	   tasks, but rather use the shadow information directly. */

	if (!thread || xnthread_get_magic(thread) != XENO_SKIN_MAGIC)
		return NULL;

	return thread2rtask(thread);	/* Convert TCB pointers. */
}

/*
 * int __rt_task_create(struct rt_arg_bulk *bulk,
 *                      xncompletion_t __user *u_completion)
 *
 * bulk = {
 * a1: RT_TASK_PLACEHOLDER *task;
 * a2: const char *name;
 * a3: int prio;
 * a4: int mode;
 * a5: pthread_t opaque;
 * a6: thread mode offset writeback area;
 * }
 */

static int __rt_task_create(struct pt_regs *regs)
{
	xncompletion_t __user *u_completion;
	struct task_struct *p = current;
	char name[XNOBJECT_NAME_LEN];
	struct rt_arg_bulk bulk;
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task = NULL;
	int err, prio, mode;

	/* Completion descriptor our parent thread is pending on -- may be NULL. */
	u_completion = (xncompletion_t __user *)__xn_reg_arg2(regs);

	if (xnshadow_thread(p)) {
		err = -EBUSY;
		goto fail;
	}

	if (__xn_safe_copy_from_user(&bulk, (void __user *)__xn_reg_arg1(regs),
				     sizeof(bulk))) {
		err = -EFAULT;
		goto fail;
	}

	if (bulk.a2) {
		if (__xn_safe_strncpy_from_user(name, (const char __user *)bulk.a2,
						sizeof(name) - 1) < 0) {
			err = -EFAULT;
			goto fail;
		}
		name[sizeof(name) - 1] = '\0';
		strncpy(p->comm, name, sizeof(p->comm));
		p->comm[sizeof(p->comm) - 1] = '\0';
	} else
		*name = '\0';

	/* Task priority. */
	prio = bulk.a3;
	/* Task init mode & CPU affinity. */
	mode = bulk.a4 & (T_CPUMASK | T_SUSP);

	task = (RT_TASK *)xnmalloc(sizeof(*task));

	if (!task) {
		err = -ENOMEM;
		goto fail;
	}

	xnthread_clear_state(&task->thread_base, XNZOMBIE);

	/* Force FPU support in user-space. This will lead to a no-op if
	   the platform does not support it. */

	err = rt_task_create(task, name, 0, prio, XNFPU | XNSHADOW | mode);
	if (err) {
		task = NULL;
		goto fail;
	}

	/* Apply CPU affinity */
	set_cpus_allowed(p, task->affinity);

	/* Copy back the registry handle to the ph struct. */
	ph.opaque = xnthread_handle(&task->thread_base);
	ph.opaque2 = bulk.a5;	/* hidden pthread_t identifier. */
	if (__xn_safe_copy_to_user((void __user *)bulk.a1, &ph, sizeof(ph))) {
		err = -EFAULT;
		goto delete;
	}

	if (!bulk.a6) {
		err = -ENOMEM;
		goto delete;
	}

	err = xnshadow_map(&task->thread_base, u_completion,
			   (unsigned long __user *)bulk.a6);
	if (err)
		goto delete;

	if (bulk.a4 & T_WARNSW)
		xnpod_set_thread_mode(&task->thread_base, 0, XNTRAPSW);

	return 0;

delete:
	rt_task_delete(task);

fail:
	/* Unblock and pass back error code. */
	if (u_completion)
		xnshadow_signal_completion(u_completion, err);

	/* Task memory could have been released by an indirect call to
	 * the deletion hook, after xnpod_delete_thread() has been
	 * issued from rt_task_create() (e.g. upon registration
	 * error). We avoid double memory release when the XNZOMBIE
	 * flag is raised, meaning the deletion hook has run, and the
	 * TCB memory is already scheduled for release. */
	if (task != NULL
	    && !xnthread_test_state(&task->thread_base, XNZOMBIE))
		xnfree(task);

	return err;
}

/*
 * int __rt_task_bind(RT_TASK_PLACEHOLDER *ph,
 *                    const char *name,
 *                    RTIME *timeoutp)
 */

static int __rt_task_bind(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_TASK_PLACEHOLDER ph;
	int err;

	err =
	    __rt_bind_helper(p, regs, &ph.opaque, XENO_TASK_MAGIC, NULL,
			     -offsetof(RT_TASK, thread_base));

	if (err)
		return err;

	/* We just don't know the associated user-space pthread
	   identifier -- clear it to prevent misuse. */
	ph.opaque2 = 0;
	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
				   sizeof(ph)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_task_start(RT_TASK_PLACEHOLDER *ph,
 *                     void (*entry)(void *cookie),
 *                     void *cookie)
 */

static int __rt_task_start(struct pt_regs *regs)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	task = __rt_task_lookup(ph.opaque);

	if (!task)
		return -ESRCH;

	return rt_task_start(task,
			     (void (*)(void *))__xn_reg_arg2(regs),
			     (void *)__xn_reg_arg3(regs));
}

/*
 * int __rt_task_suspend(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_suspend(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (__xn_reg_arg1(regs)) {
		if (__xn_safe_copy_from_user(&ph,
					     (void __user *)__xn_reg_arg1(regs),
					     sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(p);

	if (!task)
		return -ESRCH;

	return rt_task_suspend(task);
}

/*
 * int __rt_task_resume(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_resume(struct pt_regs *regs)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	task = __rt_task_lookup(ph.opaque);

	if (!task)
		return -ESRCH;

	return rt_task_resume(task);
}

/*
 * int __rt_task_delete(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_delete(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (__xn_reg_arg1(regs)) {
		if (__xn_safe_copy_from_user(&ph,
					     (void __user *)__xn_reg_arg1(regs),
					     sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(p);

	if (!task)
		return -ESRCH;

	return rt_task_delete(task);	/* TCB freed in delete hook. */
}

/*
 * int __rt_task_yield(void)
 */

static int __rt_task_yield(struct pt_regs *regs)
{
	return rt_task_yield();
}

/*
 * int __rt_task_set_periodic(RT_TASK_PLACEHOLDER *ph,
 *				 RTIME idate,
 *				 RTIME period)
 */

static int __rt_task_set_periodic(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_TASK_PLACEHOLDER ph;
	RTIME idate, period;
	RT_TASK *task;

	if (__xn_reg_arg1(regs)) {
		if (__xn_safe_copy_from_user(&ph,
					     (void __user *)__xn_reg_arg1(regs),
					     sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(p);

	if (!task)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&idate, (void __user *)__xn_reg_arg2(regs),
				     sizeof(idate)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&period, (void __user *)__xn_reg_arg3(regs),
				     sizeof(period)))
		return -EFAULT;

	return rt_task_set_periodic(task, idate, period);
}

/*
 * int __rt_task_wait_period(unsigned long *overruns_r)
 */

static int __rt_task_wait_period(struct pt_regs *regs)
{
	unsigned long overruns;
	int err;

	err = rt_task_wait_period(&overruns);

	if (__xn_reg_arg1(regs) && (err == 0 || err == -ETIMEDOUT))
		__xn_put_user(overruns,
			      (unsigned long __user *)__xn_reg_arg1(regs));
	return err;
}

/*
 * int __rt_task_set_priority(RT_TASK_PLACEHOLDER *ph,
 *                            int prio)
 */

static int __rt_task_set_priority(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;
	int prio;

	if (__xn_reg_arg1(regs)) {
		if (__xn_safe_copy_from_user(&ph,
					     (void __user *)__xn_reg_arg1(regs),
					     sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(p);

	if (!task)
		return -ESRCH;

	prio = __xn_reg_arg2(regs);

	return rt_task_set_priority(task, prio);
}

/*
 * int __rt_task_sleep(RTIME delay)
 */

static int __rt_task_sleep(struct pt_regs *regs)
{
	RTIME delay;

	if (__xn_safe_copy_from_user(&delay, (void __user *)__xn_reg_arg1(regs),
				     sizeof(delay)))
		return -EFAULT;

	return rt_task_sleep(delay);
}

/*
 * int __rt_task_sleep(RTIME delay)
 */

static int __rt_task_sleep_until(struct pt_regs *regs)
{
	RTIME date;

	if (__xn_safe_copy_from_user(&date, (void __user *)__xn_reg_arg1(regs),
				     sizeof(date)))
		return -EFAULT;

	return rt_task_sleep_until(date);
}

/*
 * int __rt_task_unblock(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_unblock(struct pt_regs *regs)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	task = __rt_task_lookup(ph.opaque);

	if (!task)
		return -ESRCH;

	return rt_task_unblock(task);
}

/*
 * int __rt_task_inquire(RT_TASK_PLACEHOLDER *ph,
 *                       RT_TASK_INFO *infop)
 */

static int __rt_task_inquire(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_TASK_PLACEHOLDER ph;
	RT_TASK_INFO info;
	RT_TASK *task;
	int err;

	if (__xn_reg_arg1(regs)) {
		if (__xn_safe_copy_from_user(&ph,
					     (void __user *)__xn_reg_arg1(regs),
					     sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(p);

	if (!task)
		return -ESRCH;

	if (unlikely(!__xn_reg_arg2(regs)))
		/* Probe for existence. */
		return 0;

	err = rt_task_inquire(task, &info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_task_notify(RT_TASK_PLACEHOLDER *ph,
 *                      rt_sigset_t signals)
 */

static int __rt_task_notify(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_TASK_PLACEHOLDER ph;
	rt_sigset_t signals;
	RT_TASK *task;

	if (__xn_reg_arg1(regs)) {
		if (__xn_safe_copy_from_user(&ph,
					     (void __user *)__xn_reg_arg1(regs),
					     sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(p);

	if (!task)
		return -ESRCH;

	signals = (rt_sigset_t)__xn_reg_arg2(regs);

	return rt_task_notify(task, signals);
}

/*
 * int __rt_task_set_mode(int clrmask,
 *                        int setmask,
 *                        int *mode_r)
 */

static int __rt_task_set_mode(struct pt_regs *regs)
{
	int err, setmask, clrmask, mode_r;

	clrmask = __xn_reg_arg1(regs);
	if (clrmask & T_CONFORMING)
		return -EINVAL;

	/*
	 * This call already required a primary mode switch, so if
	 * T_CONFORMING was specified for a real-time shadow, we are
	 * fine. If it was given from a non real-time shadow, well
	 * this is silly, and we'll be relaxed soon due to the
	 * auto-relax feature, leading to a nop.
	 */
	setmask = __xn_reg_arg2(regs) & ~T_CONFORMING;
	err = rt_task_set_mode(clrmask, setmask, &mode_r);
	if (err)
		return err;

	mode_r |= T_CONFORMING;

	if (__xn_reg_arg3(regs) &&
	    __xn_safe_copy_to_user((void __user *)__xn_reg_arg3(regs),
				   &mode_r, sizeof(mode_r)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_task_self(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_self(struct pt_regs *regs)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	task = __rt_task_current(current);

	if (!task)
		/* Calls on behalf of a non-task context beget an error for
		   the user-space interface. */
		return -ESRCH;

	ph.opaque = xnthread_handle(&task->thread_base);	/* Copy back the task handle. */

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph, sizeof(ph)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_task_slice(RT_TASK_PLACEHOLDER *ph,
 *                     RTIME quantum)
 */

static int __rt_task_slice(struct pt_regs *regs)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;
	RTIME quantum;

	if (__xn_reg_arg1(regs)) {
		if (__xn_safe_copy_from_user(&ph,
					     (void __user *)__xn_reg_arg1(regs),
					     sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (!task)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&quantum, (void __user *)__xn_reg_arg2(regs),
				     sizeof(quantum)))
		return -EFAULT;

	return rt_task_slice(task, quantum);
}

#ifdef CONFIG_XENO_OPT_NATIVE_MPS

/*
 * int __rt_task_send(RT_TASK_PLACEHOLDER *ph,
 *                    RT_TASK_MCB *mcb_s,
 *                    RT_TASK_MCB *mcb_r,
 *                    RTIME timeout)
 */

static int __rt_task_send(struct pt_regs *regs)
{
	char tmp_buf[RT_MCB_FSTORE_LIMIT];
	RT_TASK_MCB mcb_s, mcb_r;
	caddr_t tmp_area, data_r;
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;
	RTIME timeout;
	size_t xsize;
	ssize_t err;

	if (__xn_reg_arg1(regs)) {
		if (__xn_safe_copy_from_user(&ph,
					     (void __user *)__xn_reg_arg1(regs),
					     sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (!task)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&mcb_s, (void __user *)__xn_reg_arg2(regs),
				     sizeof(mcb_s)))
		return -EFAULT;

	if (__xn_reg_arg3(regs)) {
		if (__xn_safe_copy_from_user(&mcb_r,
					     (void __user *)__xn_reg_arg3(regs),
					     sizeof(mcb_r)))
			return -EFAULT;
	} else {
		mcb_r.data = NULL;
		mcb_r.size = 0;
	}

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg4(regs),
				     sizeof(timeout)))
		return -EFAULT;

	xsize = mcb_s.size + mcb_r.size;
	data_r = mcb_r.data;

	if (xsize > 0) {
		/* Try optimizing a bit here: if the cumulated message sizes
		   (initial+reply) can fit into our local buffer, use it;
		   otherwise, take the slow path and fetch a larger buffer
		   from the system heap. Most messages are expected to be
		   short enough to fit on the stack anyway. */

		if (xsize <= sizeof(tmp_buf))
			tmp_area = tmp_buf;
		else {
			tmp_area = xnmalloc(xsize);

			if (!tmp_area)
				return -ENOMEM;
		}

		if (mcb_s.size > 0 &&
		    __xn_safe_copy_from_user(tmp_area,
					     (void __user *)mcb_s.data,
					     mcb_s.size)) {
			err = -EFAULT;
			goto out;
		}

		mcb_s.data = tmp_area;
		mcb_r.data = tmp_area + mcb_s.size;
	} else
		tmp_area = NULL;

	err = rt_task_send(task, &mcb_s, &mcb_r, timeout);

	if (err > 0 &&
	    __xn_safe_copy_to_user((void __user *)data_r, mcb_r.data, mcb_r.size)) {
		err = -EFAULT;
		goto out;
	}

	if (__xn_reg_arg3(regs)) {
		mcb_r.data = data_r;
		if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg3(regs),
					   &mcb_r, sizeof(mcb_r)))
			err = -EFAULT;
	}

out:
	if (tmp_area && tmp_area != tmp_buf)
		xnfree(tmp_area);

	return err;
}

/*
 * int __rt_task_receive(RT_TASK_MCB *mcb_r,
 *                       RTIME timeout)
 */

static int __rt_task_receive(struct pt_regs *regs)
{
	char tmp_buf[RT_MCB_FSTORE_LIMIT];
	caddr_t tmp_area, data_r;
	RT_TASK_MCB mcb_r;
	RTIME timeout;
	int err;

	if (__xn_safe_copy_from_user(&mcb_r, (void __user *)__xn_reg_arg1(regs),
				     sizeof(mcb_r)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg2(regs),
				     sizeof(timeout)))
		return -EFAULT;

	data_r = mcb_r.data;

	if (mcb_r.size > 0) {
		/* Same optimization as in __rt_task_send(): if the size of
		   the reply message can fit into our local buffer, use it;
		   otherwise, take the slow path and fetch a larger buffer
		   from the system heap. */

		if (mcb_r.size <= sizeof(tmp_buf))
			tmp_area = tmp_buf;
		else {
			tmp_area = xnmalloc(mcb_r.size);

			if (!tmp_area)
				return -ENOMEM;
		}

		mcb_r.data = tmp_area;
	} else
		tmp_area = NULL;

	err = rt_task_receive(&mcb_r, timeout);

	if (err > 0 && mcb_r.size > 0) {
		if (__xn_safe_copy_to_user((void __user *)data_r, mcb_r.data,
					   mcb_r.size)) {
			err = -EFAULT;
			goto out;
		}
	}

	mcb_r.data = data_r;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &mcb_r,
				   sizeof(mcb_r)))
		err = -EFAULT;

out:
	if (tmp_area && tmp_area != tmp_buf)
		xnfree(tmp_area);

	return err;
}

/*
 * int __rt_task_reply(int flowid,
 *                     RT_TASK_MCB *mcb_s)
 */

static int __rt_task_reply(struct pt_regs *regs)
{
	char tmp_buf[RT_MCB_FSTORE_LIMIT];
	RT_TASK_MCB mcb_s;
	caddr_t tmp_area;
	int flowid, err;

	flowid = __xn_reg_arg1(regs);

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_copy_from_user(&mcb_s,
					     (void __user *)__xn_reg_arg2(regs),
					     sizeof(mcb_s)))
			return -EFAULT;
	} else {
		mcb_s.data = NULL;
		mcb_s.size = 0;
	}

	if (mcb_s.size > 0) {
		/* Same optimization as in __rt_task_send(): if the size of
		   the reply message can fit into our local buffer, use it;
		   otherwise, take the slow path and fetch a larger buffer
		   from the system heap. */

		if (mcb_s.size <= sizeof(tmp_buf))
			tmp_area = tmp_buf;
		else {
			tmp_area = xnmalloc(mcb_s.size);

			if (!tmp_area)
				return -ENOMEM;
		}

		if (__xn_safe_copy_from_user(tmp_area, (void __user *)mcb_s.data,
					     mcb_s.size)) {
			err = -EFAULT;
			goto out;
		}

		mcb_s.data = tmp_area;
	} else
		tmp_area = NULL;

	err = rt_task_reply(flowid, &mcb_s);

out:
	if (tmp_area && tmp_area != tmp_buf)
		xnfree(tmp_area);

	return err;
}

#else /* !CONFIG_XENO_OPT_NATIVE_MPS */

#define __rt_task_send     __rt_call_not_available
#define __rt_task_receive  __rt_call_not_available
#define __rt_task_reply    __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_MPS */

/*
 * int __rt_timer_set_mode(RTIME *tickvalp)
 */

static int __rt_timer_set_mode(struct pt_regs *regs)
{
	RTIME tickval;

	if (__xn_safe_copy_from_user(&tickval, (void __user *)__xn_reg_arg1(regs),
				     sizeof(tickval)))
		return -EFAULT;

	return rt_timer_set_mode(tickval);
}

/*
 * int __rt_timer_read(RTIME *timep)
 */

static int __rt_timer_read(struct pt_regs *regs)
{
	RTIME now = rt_timer_read();

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &now,
				   sizeof(now)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_timer_tsc(RTIME *tscp)
 */

static int __rt_timer_tsc(struct pt_regs *regs)
{
	RTIME tsc = rt_timer_tsc();

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &tsc,
				   sizeof(tsc)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_timer_ns2ticks(SRTIME *ticksp, SRTIME *nsp)
 */

static int __rt_timer_ns2ticks(struct pt_regs *regs)
{
	SRTIME ns, ticks;

	if (__xn_safe_copy_from_user(&ns, (void __user *)__xn_reg_arg2(regs),
				     sizeof(ns)))
		return -EFAULT;

	ticks = rt_timer_ns2ticks(ns);

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ticks,
				   sizeof(ticks)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_timer_ticks2ns(SRTIME *nsp, SRTIME *ticksp)
 */

static int __rt_timer_ticks2ns(struct pt_regs *regs)
{
	SRTIME ticks, ns;

	if (__xn_safe_copy_from_user(&ticks, (void __user *)__xn_reg_arg2(regs),
				     sizeof(ticks)))
		return -EFAULT;

	ns = rt_timer_ticks2ns(ticks);

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ns, sizeof(ns)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_timer_inquire(RT_TIMER_INFO *info)
 */

static int __rt_timer_inquire(struct pt_regs *regs)
{
	RT_TIMER_INFO info;
	int err;

	err = rt_timer_inquire(&info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_timer_spin(RTIME *nsp)
 */

static int __rt_timer_spin(struct pt_regs *regs)
{
	xnthread_t *thread = xnpod_current_thread();
	struct task_struct *p = current;
	RTIME etime;
	RTIME ns;

	if (__xn_safe_copy_from_user(&ns, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ns)))
		return -EFAULT;

	etime = xnarch_get_cpu_tsc() + xnarch_ns_to_tsc(ns);
	while ((SRTIME)(xnarch_get_cpu_tsc() - etime) < 0) {
		if (signal_pending(p) || xnthread_amok_p(thread))
			return -EINTR;
		cpu_relax();
	}

	return 0;
}

#ifdef CONFIG_XENO_OPT_NATIVE_SEM

/*
 * int __rt_sem_create(RT_SEM_PLACEHOLDER *ph,
 *                     const char *name,
 *                     unsigned icount,
 *                     int mode)
 */

static int __rt_sem_create(struct pt_regs *regs)
{
	char name[XNOBJECT_NAME_LEN];
	RT_SEM_PLACEHOLDER ph;
	unsigned icount;
	int err, mode;
	RT_SEM *sem;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;
		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	/* Initial semaphore value. */
	icount = (unsigned)__xn_reg_arg3(regs);
	/* Creation mode. */
	mode = (int)__xn_reg_arg4(regs);

	sem = (RT_SEM *)xnmalloc(sizeof(*sem));

	if (!sem)
		return -ENOMEM;

	err = rt_sem_create(sem, name, icount, mode);

	if (err == 0) {
		sem->cpid = current->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = sem->handle;
		if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
					   sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(sem);

	return err;
}

/*
 * int __rt_sem_bind(RT_SEM_PLACEHOLDER *ph,
 *                   const char *name,
 *                   RTIME *timeoutp)
 */

static int __rt_sem_bind(struct pt_regs *regs)
{
	RT_SEM_PLACEHOLDER ph;
	int err;

	err =
	    __rt_bind_helper(current, regs, &ph.opaque, XENO_SEM_MAGIC,
			     NULL, 0);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
				   sizeof(ph)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_sem_delete(RT_SEM_PLACEHOLDER *ph)
 */

static int __rt_sem_delete(struct pt_regs *regs)
{
	RT_SEM_PLACEHOLDER ph;
	RT_SEM *sem;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	sem = (RT_SEM *)xnregistry_fetch(ph.opaque);

	if (!sem)
		return -ESRCH;

	err = rt_sem_delete(sem);

	if (!err && sem->cpid)
		xnfree(sem);

	return err;
}

/*
 * int __rt_sem_p(RT_SEM_PLACEHOLDER *ph,
 *                xntmode_t timeout_mode,
 *                RTIME *timeoutp)
 */

static int __rt_sem_p(struct pt_regs *regs)
{
	xntmode_t timeout_mode;
	RT_SEM_PLACEHOLDER ph;
	RTIME timeout;
	RT_SEM *sem;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	sem = (RT_SEM *)xnregistry_fetch(ph.opaque);
	if (!sem)
		return -ESRCH;

	timeout_mode = __xn_reg_arg2(regs);

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg3(regs),
				     sizeof(timeout)))
		return -EFAULT;

	return rt_sem_p_inner(sem, timeout_mode, timeout);
}

/*
 * int __rt_sem_v(RT_SEM_PLACEHOLDER *ph)
 */

static int __rt_sem_v(struct pt_regs *regs)
{
	RT_SEM_PLACEHOLDER ph;
	RT_SEM *sem;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	sem = (RT_SEM *)xnregistry_fetch(ph.opaque);

	if (!sem)
		return -ESRCH;

	return rt_sem_v(sem);
}

/*
 * int __rt_sem_broadcast(RT_SEM_PLACEHOLDER *ph)
 */

static int __rt_sem_broadcast(struct pt_regs *regs)
{
	RT_SEM_PLACEHOLDER ph;
	RT_SEM *sem;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	sem = (RT_SEM *)xnregistry_fetch(ph.opaque);

	if (!sem)
		return -ESRCH;

	return rt_sem_broadcast(sem);
}

/*
 * int __rt_sem_inquire(RT_SEM_PLACEHOLDER *ph,
 *                      RT_SEM_INFO *infop)
 */

static int __rt_sem_inquire(struct pt_regs *regs)
{
	RT_SEM_PLACEHOLDER ph;
	RT_SEM_INFO info;
	RT_SEM *sem;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	sem = (RT_SEM *)xnregistry_fetch(ph.opaque);

	if (!sem)
		return -ESRCH;

	err = rt_sem_inquire(sem, &info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

#else /* !CONFIG_XENO_OPT_NATIVE_SEM */

#define __rt_sem_create    __rt_call_not_available
#define __rt_sem_bind      __rt_call_not_available
#define __rt_sem_delete    __rt_call_not_available
#define __rt_sem_p         __rt_call_not_available
#define __rt_sem_v         __rt_call_not_available
#define __rt_sem_broadcast __rt_call_not_available
#define __rt_sem_inquire   __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_SEM */

#ifdef CONFIG_XENO_OPT_NATIVE_EVENT

/*
 * int __rt_event_create(RT_EVENT_PLACEHOLDER *ph,
 *                       const char *name,
 *                       unsigned ivalue,
 *                       int mode)
 */

static int __rt_event_create(struct pt_regs *regs)
{
	char name[XNOBJECT_NAME_LEN];
	RT_EVENT_PLACEHOLDER ph;
	unsigned ivalue;
	RT_EVENT *event;
	int err, mode;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	/* Initial event mask value. */
	ivalue = (unsigned)__xn_reg_arg3(regs);
	/* Creation mode. */
	mode = (int)__xn_reg_arg4(regs);

	event = (RT_EVENT *)xnmalloc(sizeof(*event));

	if (!event)
		return -ENOMEM;

	err = rt_event_create(event, name, ivalue, mode);

	if (err == 0) {
		event->cpid = current->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = event->handle;
		if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
					   sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(event);

	return err;
}

/*
 * int __rt_event_bind(RT_EVENT_PLACEHOLDER *ph,
 *                     const char *name,
 *                     RTIME *timeoutp)
 */

static int __rt_event_bind(struct pt_regs *regs)
{
	RT_EVENT_PLACEHOLDER ph;
	int err;

	err =
	    __rt_bind_helper(current, regs, &ph.opaque, XENO_EVENT_MAGIC,
			     NULL, 0);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
				   sizeof(ph)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_event_delete(RT_EVENT_PLACEHOLDER *ph)
 */

static int __rt_event_delete(struct pt_regs *regs)
{
	RT_EVENT_PLACEHOLDER ph;
	RT_EVENT *event;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	event = (RT_EVENT *)xnregistry_fetch(ph.opaque);

	if (!event)
		return -ESRCH;

	err = rt_event_delete(event);

	if (!err && event->cpid)
		xnfree(event);

	return err;
}

/*
 * int __rt_event_wait(RT_EVENT_PLACEHOLDER *ph,
 *                     unsigned long *mask_io,
 *                     int mode,
 *                     xntmode_t timeout_mode,
 *                     RTIME *timeoutp)
 */

static int __rt_event_wait(struct pt_regs *regs)
{
	unsigned long mask, mask_r;
	RT_EVENT_PLACEHOLDER ph;
	xntmode_t timeout_mode;
	RT_EVENT *event;
	RTIME timeout;
	int mode, err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&mask, (void __user *)__xn_reg_arg2(regs),
				     sizeof(mask)))
		return -EFAULT;

	event = (RT_EVENT *)xnregistry_fetch(ph.opaque);
	if (!event)
		return -ESRCH;

	mode = (int)__xn_reg_arg3(regs);
	timeout_mode = __xn_reg_arg4(regs);

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg5(regs),
				     sizeof(timeout)))
		return -EFAULT;

	err = rt_event_wait_inner(event, mask, &mask_r, mode, timeout_mode, timeout);

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs), &mask_r,
				   sizeof(mask_r)))
		return -EFAULT;

	return err;
}

/*
 * int __rt_event_signal(RT_EVENT_PLACEHOLDER *ph,
 *                       unsigned long mask)
 */

static int __rt_event_signal(struct pt_regs *regs)
{
	RT_EVENT_PLACEHOLDER ph;
	unsigned long mask;
	RT_EVENT *event;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	event = (RT_EVENT *)xnregistry_fetch(ph.opaque);

	if (!event)
		return -ESRCH;

	mask = (unsigned long)__xn_reg_arg2(regs);

	return rt_event_signal(event, mask);
}

/*
 * int __rt_event_clear(RT_EVENT_PLACEHOLDER *ph,
 *                      unsigned long mask,
 *                      unsigned long *mask_r)
 */

static int __rt_event_clear(struct pt_regs *regs)
{
	unsigned long mask, mask_r;
	RT_EVENT_PLACEHOLDER ph;
	RT_EVENT *event;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	event = (RT_EVENT *)xnregistry_fetch(ph.opaque);

	if (!event)
		return -ESRCH;

	mask = (unsigned long)__xn_reg_arg2(regs);

	err = rt_event_clear(event, mask, &mask_r);

	if (likely(!err && __xn_reg_arg3(regs)))
		if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg3(regs),
					   &mask_r, sizeof(mask_r)))
			err = -EFAULT;
	return err;
}

/*
 * int __rt_event_inquire(RT_EVENT_PLACEHOLDER *ph,
 *                        RT_EVENT_INFO *infop)
 */

static int __rt_event_inquire(struct pt_regs *regs)
{
	RT_EVENT_PLACEHOLDER ph;
	RT_EVENT_INFO info;
	RT_EVENT *event;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	event = (RT_EVENT *)xnregistry_fetch(ph.opaque);

	if (!event)
		return -ESRCH;

	err = rt_event_inquire(event, &info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

#else /* !CONFIG_XENO_OPT_NATIVE_EVENT */

#define __rt_event_create  __rt_call_not_available
#define __rt_event_bind    __rt_call_not_available
#define __rt_event_delete  __rt_call_not_available
#define __rt_event_wait    __rt_call_not_available
#define __rt_event_signal  __rt_call_not_available
#define __rt_event_clear   __rt_call_not_available
#define __rt_event_inquire __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_EVENT */

#ifdef CONFIG_XENO_OPT_NATIVE_MUTEX

/*
 * int __rt_mutex_create(RT_MUTEX_PLACEHOLDER *ph,
 *                       const char *name)
 */

static int __rt_mutex_create(struct pt_regs *regs)
{
	char name[XNOBJECT_NAME_LEN];
	xnheap_t *sem_heap;
	RT_MUTEX_PLACEHOLDER ph;
	RT_MUTEX *mutex;
	int err;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	sem_heap = &xnsys_ppd_get(*name != '\0')->sem_heap;

	mutex = (RT_MUTEX *)xnmalloc(sizeof(*mutex));

	if (!mutex)
		return -ENOMEM;

	err = rt_mutex_create_inner(mutex, name, *name != '\0');
	if (err < 0)
		goto err_free_mutex;

	mutex->cpid = current->pid;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = mutex->handle;
#ifdef CONFIG_XENO_FASTSYNCH
		/* The lock address will be finished in user space. */
	ph.fastlock =
		(void *)xnheap_mapped_offset(sem_heap,
					     mutex->synch_base.fastlock);
#endif /* CONFIG_XENO_FASTSYNCH */
	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
				   sizeof(ph))) {
		err = -EFAULT;
		goto err_delete_mutex;
	}

	return 0;

  err_delete_mutex:
	rt_mutex_delete(mutex);
  err_free_mutex:
	xnfree(mutex);
	return err;
}

/*
 * int __rt_mutex_bind(RT_MUTEX_PLACEHOLDER *ph,
 *                     const char *name,
 *                     RTIME *timeoutp)
 */

static int __rt_mutex_bind(struct pt_regs *regs)
{
	RT_MUTEX_PLACEHOLDER ph;
	RT_MUTEX *mutex;
	int err;

	err =
	    __rt_bind_helper(current, regs, &ph.opaque, XENO_MUTEX_MAGIC,
			     (void **)&mutex, 0);

	if (err)
		return err;

#ifdef CONFIG_XENO_FASTSYNCH
	ph.fastlock =
		(void *)xnheap_mapped_offset(&xnsys_ppd_get(1)->sem_heap,
					     mutex->synch_base.fastlock);
#endif /* CONFIG_XENO_FASTSYNCH */

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
			      sizeof(ph)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_mutex_delete(RT_MUTEX_PLACEHOLDER *ph)
 */

static int __rt_mutex_delete(struct pt_regs *regs)
{
	RT_MUTEX_PLACEHOLDER ph;
	RT_MUTEX *mutex;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	mutex = (RT_MUTEX *)xnregistry_fetch(ph.opaque);

	if (!mutex)
		return -ESRCH;

	err = rt_mutex_delete(mutex);

	if (!err && mutex->cpid)
		xnfree(mutex);

	return err;
}

/*
 * int __rt_mutex_acquire(RT_MUTEX_PLACEHOLDER *ph,
 *			  xntmode_t timeout_mode,
 *                        RTIME *timeoutp)
 */

static int __rt_mutex_acquire(struct pt_regs *regs)
{
	RT_MUTEX_PLACEHOLDER __user *ph;
	xntmode_t timeout_mode;
	xnhandle_t mutexh;
	RT_MUTEX *mutex;
	RTIME timeout;

	ph = (RT_MUTEX_PLACEHOLDER __user *)__xn_reg_arg1(regs);
	if (__xn_safe_copy_from_user(&mutexh, &ph->opaque, sizeof(mutexh)))
		return -EFAULT;

	timeout_mode = __xn_reg_arg2(regs);

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg3(regs),
				     sizeof(timeout)))
		return -EFAULT;

	mutex = (RT_MUTEX *)xnregistry_fetch(mutexh);

	if (!mutex)
		return -ESRCH;

	return rt_mutex_acquire_inner(mutex, timeout, timeout_mode);
}

/*
 * int __rt_mutex_release(RT_MUTEX_PLACEHOLDER *ph)
 */

static int __rt_mutex_release(struct pt_regs *regs)
{
	RT_MUTEX_PLACEHOLDER __user *ph;
	xnhandle_t mutexh;
	RT_MUTEX *mutex;

	ph = (RT_MUTEX_PLACEHOLDER __user *)__xn_reg_arg1(regs);
	if (__xn_safe_copy_from_user(&mutexh, &ph->opaque, sizeof(mutexh)))
		return -EFAULT;

	mutex = (RT_MUTEX *)xnregistry_fetch(mutexh);

	if (!mutex)
		return -ESRCH;

	return rt_mutex_release(mutex);
}

/*
 * int __rt_mutex_inquire(RT_MUTEX_PLACEHOLDER *ph,
 *                        RT_MUTEX_INFO *infop)
 */

static int __rt_mutex_inquire(struct pt_regs *regs)
{
	RT_MUTEX_PLACEHOLDER ph;
	RT_MUTEX_INFO info;
	RT_MUTEX *mutex;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	mutex = (RT_MUTEX *)xnregistry_fetch(ph.opaque);

	if (!mutex)
		return -ESRCH;

	err = rt_mutex_inquire(mutex, &info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

#else /* !CONFIG_XENO_OPT_NATIVE_MUTEX */

#define __rt_mutex_create  __rt_call_not_available
#define __rt_mutex_bind    __rt_call_not_available
#define __rt_mutex_delete  __rt_call_not_available
#define __rt_mutex_acquire __rt_call_not_available
#define __rt_mutex_release __rt_call_not_available
#define __rt_mutex_inquire __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_MUTEX */

#ifdef CONFIG_XENO_OPT_NATIVE_COND

/*
 * int __rt_cond_create(RT_COND_PLACEHOLDER *ph,
 *                      const char *name)
 */

static int __rt_cond_create(struct pt_regs *regs)
{
	char name[XNOBJECT_NAME_LEN];
	RT_COND_PLACEHOLDER ph;
	RT_COND *cond;
	int err;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	cond = (RT_COND *)xnmalloc(sizeof(*cond));

	if (!cond)
		return -ENOMEM;

	err = rt_cond_create(cond, name);

	if (err == 0) {
		cond->cpid = current->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = cond->handle;
		if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
					   sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(cond);

	return err;
}

/*
 * int __rt_cond_bind(RT_COND_PLACEHOLDER *ph,
 *                    const char *name,
 *                    RTIME *timeoutp)
 */

static int __rt_cond_bind(struct pt_regs *regs)
{
	RT_COND_PLACEHOLDER ph;
	int err;

	err =
	    __rt_bind_helper(current, regs, &ph.opaque, XENO_COND_MAGIC,
			     NULL, 0);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
				   sizeof(ph)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_cond_delete(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_delete(struct pt_regs *regs)
{
	RT_COND_PLACEHOLDER ph;
	RT_COND *cond;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	cond = (RT_COND *)xnregistry_fetch(ph.opaque);

	if (!cond)
		return -ESRCH;

	err = rt_cond_delete(cond);

	if (!err && cond->cpid)
		xnfree(cond);

	return err;
}

/*
 * int __rt_cond_wait_prologue(RT_COND_PLACEHOLDER *cph,
 *		      	       RT_MUTEX_PLACEHOLDER *mph,
 *		      	       unsigned *plockcnt,
 *		      	       xntmode_t timeout_mode,
 *		      	       RTIME *timeoutp)
 */

struct us_cond_data {
	unsigned lockcnt;
	int err;
};

static int __rt_cond_wait_prologue(struct pt_regs *regs)
{
	RT_COND_PLACEHOLDER cph, mph;
	unsigned dummy, *plockcnt;
	xntmode_t timeout_mode;
	struct us_cond_data d;
	int err, perr = 0;
	RT_MUTEX *mutex;
	RT_COND *cond;
	RTIME timeout;

	if (__xn_safe_copy_from_user(&cph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(cph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&mph, (void __user *)__xn_reg_arg2(regs),
				     sizeof(mph)))
		return -EFAULT;

	cond = (RT_COND *)xnregistry_fetch(cph.opaque);

	if (!cond)
		return -ESRCH;

	mutex = (RT_MUTEX *)xnregistry_fetch(mph.opaque);

	if (!mutex)
		return -ESRCH;

	timeout_mode = __xn_reg_arg4(regs);

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg5(regs),
				     sizeof(timeout)))
		return -EFAULT;

#ifdef CONFIG_XENO_FASTSYNCH
	if (__xn_safe_copy_from_user(&d, (void __user *)__xn_reg_arg3(regs),
				     sizeof(d)))
		return -EFAULT;

	plockcnt = &dummy;
#else /* !CONFIG_XENO_FASTSYNCH */
	plockcnt = &d.lockcnt;
	(void)dummy;
#endif /* !CONFIG_XENO_FASTSYNCH */

	err = rt_cond_wait_prologue(cond, mutex, plockcnt, timeout_mode, timeout);

	switch(err) {
	case 0:
	case -ETIMEDOUT:
	case -EIDRM:
		perr = d.err = err;
		err = rt_cond_wait_epilogue(mutex, *plockcnt);
		break;

	case -EINTR:
		perr = err;
		d.err = 0; /* epilogue should return 0. */
		break;
	}

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg3(regs),
				   &d, sizeof(d)))
		return -EFAULT;

	return err == 0 ? perr : err;
}

/*
 * int __rt_cond_wait_epilogue(RT_MUTEX_PLACEHOLODER *mph, unsigned lockcnt)
 */

static int __rt_cond_wait_epilogue(struct pt_regs *regs)
{
	RT_COND_PLACEHOLDER mph;
	unsigned lockcnt;
	RT_MUTEX *mutex;
	int err;

	if (__xn_safe_copy_from_user(&mph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(mph)))
		return -EFAULT;

	mutex = (RT_MUTEX *)xnregistry_fetch(mph.opaque);

	if (!mutex)
		return -ESRCH;

	lockcnt = __xn_reg_arg2(regs);

	err = rt_cond_wait_epilogue(mutex, lockcnt);

	return err;
}

/*
 * int __rt_cond_signal(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_signal(struct pt_regs *regs)
{
	RT_COND_PLACEHOLDER ph;
	RT_COND *cond;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	cond = (RT_COND *)xnregistry_fetch(ph.opaque);

	if (!cond)
		return -ESRCH;

	return rt_cond_signal(cond);
}

/*
 * int __rt_cond_broadcast(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_broadcast(struct pt_regs *regs)
{
	RT_COND_PLACEHOLDER ph;
	RT_COND *cond;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	cond = (RT_COND *)xnregistry_fetch(ph.opaque);

	if (!cond)
		return -ESRCH;

	return rt_cond_broadcast(cond);
}

/*
 * int __rt_cond_inquire(RT_COND_PLACEHOLDER *ph,
 *                       RT_COND_INFO *infop)
 */

static int __rt_cond_inquire(struct pt_regs *regs)
{
	RT_COND_PLACEHOLDER ph;
	RT_COND_INFO info;
	RT_COND *cond;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	cond = (RT_COND *)xnregistry_fetch(ph.opaque);

	if (!cond)
		return -ESRCH;

	err = rt_cond_inquire(cond, &info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

#else /* !CONFIG_XENO_OPT_NATIVE_COND */

#define __rt_cond_create    	__rt_call_not_available
#define __rt_cond_bind	    	__rt_call_not_available
#define __rt_cond_delete    	__rt_call_not_available
#define __rt_cond_wait_prologue __rt_call_not_available
#define __rt_cond_wait_epilogue __rt_call_not_available
#define __rt_cond_signal    	__rt_call_not_available
#define __rt_cond_broadcast 	__rt_call_not_available
#define __rt_cond_inquire   	__rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_COND */

#ifdef CONFIG_XENO_OPT_NATIVE_QUEUE

/*
 * int __rt_queue_create(RT_QUEUE_PLACEHOLDER *ph,
 *                       const char *name,
 *                       size_t poolsize,
 *                       size_t qlimit,
 *                       int mode)
 */

static int __rt_queue_create(struct pt_regs *regs)
{
	char name[XNOBJECT_NAME_LEN];
	RT_QUEUE_PLACEHOLDER ph;
	size_t poolsize, qlimit;
	int err, mode;
	RT_QUEUE *q;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	/* Size of memory pool. */
	poolsize = (size_t) __xn_reg_arg3(regs);
	/* Queue limit. */
	qlimit = (size_t) __xn_reg_arg4(regs);
	/* Creation mode. */
	mode = (int)__xn_reg_arg5(regs);

	q = (RT_QUEUE *)xnmalloc(sizeof(*q));

	if (!q)
		return -ENOMEM;

	err = rt_queue_create(q, name, poolsize, qlimit, mode);

	if (err)
		goto free_and_fail;

	q->cpid = current->pid;

	/* Copy back the registry handle to the ph struct. */
	ph.opaque = q->handle;
	ph.opaque2 = &q->bufpool;
	ph.mapsize = xnheap_extentsize(&q->bufpool);
	ph.area = xnheap_base_memory(&q->bufpool);
	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph, sizeof(ph)))
		return -EFAULT;

	return 0;

      free_and_fail:

	xnfree(q);

	return err;
}

/*
 * int __rt_queue_bind(RT_QUEUE_PLACEHOLDER *ph,
 *                     const char *name,
 *                     RTIME *timeoutp)
 */

static int __rt_queue_bind(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;
	int err;
	spl_t s;

	err =
	    __rt_bind_helper(p, regs, &ph.opaque, XENO_QUEUE_MAGIC,
			     (void **)&q, 0);

	if (err)
		return err;

	xnlock_get_irqsave(&nklock, s);
	if (xeno_test_magic(q, XENO_QUEUE_MAGIC) == 0) {
		xnlock_put_irqrestore(&nklock, s);

		return -EACCES;
	}
	ph.opaque2 = &q->bufpool;
	ph.mapsize = xnheap_extentsize(&q->bufpool);
	ph.area = xnheap_base_memory(&q->bufpool);
	xnlock_put_irqrestore(&nklock, s);

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph, sizeof(ph)))
		return -EFAULT;

	/* We might need to migrate to secondary mode now for mapping the
	   pool memory to user-space; since this syscall is conforming, we
	   might have entered it in primary mode. */

	if (xnpod_primary_p())
		xnshadow_relax(0, 0);

	return 0;
}

/*
 * int __rt_queue_delete(RT_QUEUE_PLACEHOLDER *ph)
 */

static int __rt_queue_delete(struct pt_regs *regs)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);
	if (!q)
		return -ESRCH;

	/* Callee will check the queue descriptor for validity again. */
	return rt_queue_delete_inner(q, (void __user *)ph.mapbase);
}

/*
 * int __rt_queue_alloc(RT_QUEUE_PLACEHOLDER *ph,
 *                     size_t size,
 *                     void **bufp)
 */

static int __rt_queue_alloc(struct pt_regs *regs)
{
	RT_QUEUE_PLACEHOLDER ph;
	size_t size;
	RT_QUEUE *q;
	int err = 0;
	void *buf;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

	if (!q) {
		err = -ESRCH;
		buf = NULL;
		goto unlock_and_exit;
	}

	size = (size_t) __xn_reg_arg2(regs);

	buf = rt_queue_alloc(q, size);

	/* Convert the kernel-based address of buf to the equivalent area
	   into the caller's address space. */

	if (buf)
		buf = ph.mapbase + xnheap_mapped_offset(&q->bufpool, buf);
	else
		err = -ENOMEM;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg3(regs), &buf,
				   sizeof(buf)))
		return -EFAULT;

	return err;
}

/*
 * int __rt_queue_free(RT_QUEUE_PLACEHOLDER *ph,
 *                     void *buf)
 */

static int __rt_queue_free(struct pt_regs *regs)
{
	RT_QUEUE_PLACEHOLDER ph;
	void __user *buf;
	RT_QUEUE *q;
	int err;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	buf = (void __user *)__xn_reg_arg2(regs);

	xnlock_get_irqsave(&nklock, s);

	q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

	if (!q) {
		err = -ESRCH;
		goto unlock_and_exit;
	}

	/* Convert the caller-based address of buf to the equivalent area
	   into the kernel address space. We don't know whether buf is
	   valid memory yet, do not dereference it. */

	if (buf) {
		buf =
		    xnheap_mapped_address(&q->bufpool,
					  (caddr_t) buf - ph.mapbase);
		err = rt_queue_free(q, buf);
	} else
		err = -EINVAL;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/*
 * int __rt_queue_send(RT_QUEUE_PLACEHOLDER *ph,
 *                     void *buf,
 *                     size_t size,
 *                     int mode)
 */

static int __rt_queue_send(struct pt_regs *regs)
{
	RT_QUEUE_PLACEHOLDER ph;
	void __user *buf;
	int err, mode;
	RT_QUEUE *q;
	size_t size;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	/* Buffer to send. */
	buf = (void __user *)__xn_reg_arg2(regs);

	/* Message's payload size. */
	size = (size_t) __xn_reg_arg3(regs);

	/* Sending mode. */
	mode = (int)__xn_reg_arg4(regs);

	xnlock_get_irqsave(&nklock, s);

	q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

	if (!q) {
		err = -ESRCH;
		goto unlock_and_exit;
	}

	/* Convert the caller-based address of buf to the equivalent area
	   into the kernel address space. We don't know whether buf is
	   valid memory yet, do not dereference it. */

	if (buf) {
		buf =
		    xnheap_mapped_address(&q->bufpool,
					  (caddr_t) buf - ph.mapbase);
		err = rt_queue_send(q, buf, size, mode);
	} else
		err = -EINVAL;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/*
 * int __rt_queue_write(RT_QUEUE_PLACEHOLDER *ph,
 *                      const void *buf,
 *                      size_t size,
 *                      int mode)
 */

static int __rt_queue_write(struct pt_regs *regs)
{
	RT_QUEUE_PLACEHOLDER ph;
	void __user *buf, *mbuf;
	int mode, ret;
	RT_QUEUE *q;
	size_t size;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

	if (!q)
		return -ESRCH;

	/* Buffer to write to the queue. */
	buf = (void __user *)__xn_reg_arg2(regs);

	/* Payload size. */
	size = (size_t) __xn_reg_arg3(regs);

	/* Sending mode. */
	mode = (int)__xn_reg_arg4(regs);

	mbuf = rt_queue_alloc(q, size);

	if (!mbuf)
		return -ENOMEM;

	if (size > 0) {
		/* Slurp the message directly into the conveying buffer. */
		if (__xn_safe_copy_from_user(mbuf, buf, size)) {
			rt_queue_free(q, mbuf);
			return -EFAULT;
		}
	}

	ret = rt_queue_send(q, mbuf, size, mode);
	if (ret < 0 || (ret == 0 && (mode & Q_BROADCAST)))
		rt_queue_free(q, mbuf);

	return ret;
}

/*
 * int __rt_queue_receive(RT_QUEUE_PLACEHOLDER *ph,
 *                        void **bufp,
 *                        xntmode_t timeout_mode,
 *                        RTIME *timeoutp)
 */

static int __rt_queue_receive(struct pt_regs *regs)
{
	RT_QUEUE_PLACEHOLDER ph;
	xntmode_t timeout_mode;
	RTIME timeout;
	RT_QUEUE *q;
	void *buf;
	int err;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg4(regs),
				     sizeof(timeout)))
		return -EFAULT;

	timeout_mode = __xn_reg_arg3(regs);

	xnlock_get_irqsave(&nklock, s);

	q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

	if (!q) {
		xnlock_put_irqrestore(&nklock, s);
		err = -ESRCH;
		goto out;
	}

	err = (int)rt_queue_receive_inner(q, &buf, timeout_mode, timeout);

	/* Convert the caller-based address of buf to the equivalent area
	   into the kernel address space. */

	if (err < 0) {
		xnlock_put_irqrestore(&nklock, s);
		goto out;
	}

	/* Convert the kernel-based address of buf to the equivalent area
	   into the caller's address space. */

	buf = ph.mapbase + xnheap_mapped_offset(&q->bufpool, buf);

	xnlock_put_irqrestore(&nklock, s);

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &buf, sizeof(buf)))
		err = -EFAULT;
out:

	return err;
}

/*
 * int __rt_queue_read(RT_QUEUE_PLACEHOLDER *ph,
 *                     void *buf,
 *                     size_t size,
 *                     xntmode_t timeout_mode,
 *                     RTIME *timeoutp)
 */

static int __rt_queue_read(struct pt_regs *regs)
{
	RT_QUEUE_PLACEHOLDER ph;
	void __user *buf, *mbuf;
	xntmode_t timeout_mode;
	ssize_t rsize;
	RTIME timeout;
	RT_QUEUE *q;
	size_t size;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

	if (!q)
		return -ESRCH;

	/* Address of message space to write to. */
	buf = (void __user *)__xn_reg_arg2(regs);

	/* Size of message space. */
	size = (size_t) __xn_reg_arg3(regs);

	/* Relative/absolute timeout spec. */
	timeout_mode = __xn_reg_arg4(regs);

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg5(regs),
				     sizeof(timeout)))
		return -EFAULT;

	rsize = rt_queue_receive_inner(q, &mbuf, timeout_mode, timeout);

	if (rsize >= 0) {
		size = size < rsize ? size : rsize;

		if (size > 0 &&	__xn_safe_copy_to_user(buf, mbuf, size))
			rsize = -EFAULT;

		rt_queue_free(q, mbuf);
	}

	return (int)rsize;
}

/*
 * int __rt_queue_inquire(RT_QUEUE_PLACEHOLDER *ph,
 *                        RT_QUEUE_INFO *infop)
 */

static int __rt_queue_inquire(struct pt_regs *regs)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE_INFO info;
	RT_QUEUE *q;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

	if (!q)
		return -ESRCH;

	err = rt_queue_inquire(q, &info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_queue_flush(RT_QUEUE_PLACEHOLDER *ph)
 */

static int __rt_queue_flush(struct pt_regs *regs)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	q = xnregistry_fetch(ph.opaque);
	if (q == NULL)
		return -ESRCH;

	return rt_queue_flush(q);
}

#else /* !CONFIG_XENO_OPT_NATIVE_QUEUE */

#define __rt_queue_create    __rt_call_not_available
#define __rt_queue_bind      __rt_call_not_available
#define __rt_queue_delete    __rt_call_not_available
#define __rt_queue_alloc     __rt_call_not_available
#define __rt_queue_free      __rt_call_not_available
#define __rt_queue_send      __rt_call_not_available
#define __rt_queue_receive   __rt_call_not_available
#define __rt_queue_inquire   __rt_call_not_available
#define __rt_queue_read      __rt_call_not_available
#define __rt_queue_write     __rt_call_not_available
#define __rt_queue_flush     __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_QUEUE */

#ifdef CONFIG_XENO_OPT_NATIVE_HEAP

/*
 * int __rt_heap_create(RT_HEAP_PLACEHOLDER *ph,
 *                      const char *name,
 *                      size_t heapsize,
 *                      int mode)
 */

static int __rt_heap_create(struct pt_regs *regs)
{
	struct task_struct *p = current;
	char name[XNOBJECT_NAME_LEN];
	RT_HEAP_PLACEHOLDER ph;
	size_t heapsize;
	int err, mode;
	RT_HEAP *heap;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	/* Size of heap space. */
	heapsize = (size_t) __xn_reg_arg3(regs);
	/* Creation mode. */
	mode = (int)__xn_reg_arg4(regs);

	heap = (RT_HEAP *)xnmalloc(sizeof(*heap));

	if (!heap)
		return -ENOMEM;

	err = rt_heap_create(heap, name, heapsize, mode);

	if (err)
		goto free_and_fail;

	heap->cpid = p->pid;

	/* Copy back the registry handle to the ph struct. */
	ph.opaque = heap->handle;
	ph.opaque2 = &heap->heap_base;
	ph.mapsize = xnheap_extentsize(&heap->heap_base);
	ph.area = xnheap_base_memory(&heap->heap_base);
	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph, sizeof(ph)))
		return -EFAULT;

	return 0;

      free_and_fail:

	xnfree(heap);

	return err;
}

/*
 * int __rt_heap_bind(RT_HEAP_PLACEHOLDER *ph,
 *                    const char *name,
 *                    RTIME *timeoutp)
 */

static int __rt_heap_bind(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_HEAP_PLACEHOLDER ph;
	RT_HEAP *heap;
	int err;
	spl_t s;

	err =
	    __rt_bind_helper(p, regs, &ph.opaque, XENO_HEAP_MAGIC,
			     (void **)&heap, 0);

	if (err)
		return err;

	xnlock_get_irqsave(&nklock, s);
	if (xeno_test_magic(heap, XENO_HEAP_MAGIC) == 0) {
		xnlock_put_irqrestore(&nklock, s);

		return -EACCES;
	}
	ph.opaque2 = &heap->heap_base;
	ph.mapsize = xnheap_extentsize(&heap->heap_base);
	ph.area = xnheap_base_memory(&heap->heap_base);

	xnlock_put_irqrestore(&nklock, s);

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph, sizeof(ph)))
		return -EFAULT;

	/* We might need to migrate to secondary mode now for mapping the
	   heap memory to user-space; since this syscall is conforming, we
	   might have entered it in primary mode. */

	if (xnpod_primary_p())
		xnshadow_relax(0, 0);

	return 0;
}

/*
 * int __rt_heap_delete(RT_HEAP_PLACEHOLDER *ph)
 */

static int __rt_heap_delete(struct pt_regs *regs)
{
	RT_HEAP_PLACEHOLDER ph;
	RT_HEAP *heap;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	heap = (RT_HEAP *)xnregistry_fetch(ph.opaque);

	if (!heap)
		return -ESRCH;

	/* Callee will check the heap descriptor for validity again. */
	return rt_heap_delete_inner(heap, (void __user *)ph.mapbase);
}

/*
 * int __rt_heap_alloc(RT_HEAP_PLACEHOLDER *ph,
 *                     size_t size,
 *                     RTIME timeout,
 *                     void **bufp)
 */

static int __rt_heap_alloc(struct pt_regs *regs)
{
	RT_HEAP_PLACEHOLDER ph;
	void *buf = NULL;
	RT_HEAP *heap;
	RTIME timeout;
	size_t size;
	int err = 0;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg3(regs),
				     sizeof(timeout)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	heap = (RT_HEAP *)xnregistry_fetch(ph.opaque);

	if (!heap) {
		err = -ESRCH;
		goto unlock_and_exit;
	}

	size = (size_t) __xn_reg_arg2(regs);

	err = rt_heap_alloc(heap, size, timeout, &buf);

	/* Convert the kernel-based address of buf to the equivalent area
	   into the caller's address space. */

	if (!err)
		buf = ph.mapbase + xnheap_mapped_offset(&heap->heap_base, buf);

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg4(regs), &buf,
				   sizeof(buf)))
		return -EFAULT;

	return err;
}

/*
 * int __rt_heap_free(RT_HEAP_PLACEHOLDER *ph,
 *                    void *buf)
 */

static int __rt_heap_free(struct pt_regs *regs)
{
	RT_HEAP_PLACEHOLDER ph;
	void __user *buf;
	RT_HEAP *heap;
	int err;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	buf = (void __user *)__xn_reg_arg2(regs);

	xnlock_get_irqsave(&nklock, s);

	heap = (RT_HEAP *)xnregistry_fetch(ph.opaque);

	if (!heap) {
		err = -ESRCH;
		goto unlock_and_exit;
	}

	/* Convert the caller-based address of buf to the equivalent area
	   into the kernel address space. */

	if (buf) {
		buf =
		    xnheap_mapped_address(&heap->heap_base,
					  (caddr_t) buf - ph.mapbase);
		err = rt_heap_free(heap, buf);
	} else
		err = -EINVAL;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/*
 * int __rt_heap_inquire(RT_HEAP_PLACEHOLDER *ph,
 *                       RT_HEAP_INFO *infop)
 */

static int __rt_heap_inquire(struct pt_regs *regs)
{
	RT_HEAP_PLACEHOLDER ph;
	RT_HEAP_INFO info;
	RT_HEAP *heap;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	heap = (RT_HEAP *)xnregistry_fetch(ph.opaque);

	if (!heap)
		return -ESRCH;

	err = rt_heap_inquire(heap, &info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

#else /* !CONFIG_XENO_OPT_NATIVE_HEAP */

#define __rt_heap_create    __rt_call_not_available
#define __rt_heap_bind      __rt_call_not_available
#define __rt_heap_delete    __rt_call_not_available
#define __rt_heap_alloc     __rt_call_not_available
#define __rt_heap_free      __rt_call_not_available
#define __rt_heap_inquire   __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_HEAP */

#ifdef CONFIG_XENO_OPT_NATIVE_ALARM

void rt_alarm_handler(RT_ALARM *alarm, void *cookie)
{
	/* Wake up all tasks waiting for the alarm. */
	xnsynch_flush(&alarm->synch_base, 0);
}

EXPORT_SYMBOL_GPL(rt_alarm_handler);

/*
 * int __rt_alarm_create(RT_ALARM_PLACEHOLDER *ph,
 *                       const char *name)
 */

static int __rt_alarm_create(struct pt_regs *regs)
{
	struct task_struct *p = current;
	char name[XNOBJECT_NAME_LEN];
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM *alarm;
	int err;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	alarm = (RT_ALARM *)xnmalloc(sizeof(*alarm));

	if (!alarm)
		return -ENOMEM;

	err = rt_alarm_create(alarm, name, &rt_alarm_handler, NULL);

	if (likely(err == 0)) {
		alarm->cpid = p->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = alarm->handle;
		if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
					   sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(alarm);

	return err;
}

/*
 * int __rt_alarm_delete(RT_ALARM_PLACEHOLDER *ph)
 */

static int __rt_alarm_delete(struct pt_regs *regs)
{
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM *alarm;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	alarm = (RT_ALARM *)xnregistry_fetch(ph.opaque);

	if (!alarm)
		return -ESRCH;

	err = rt_alarm_delete(alarm);

	if (!err && alarm->cpid)
		xnfree(alarm);

	return err;
}

/*
 * int __rt_alarm_start(RT_ALARM_PLACEHOLDER *ph,
 *			RTIME value,
 *			RTIME interval)
 */

static int __rt_alarm_start(struct pt_regs *regs)
{
	RT_ALARM_PLACEHOLDER ph;
	RTIME value, interval;
	RT_ALARM *alarm;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	alarm = (RT_ALARM *)xnregistry_fetch(ph.opaque);

	if (!alarm)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&value, (void __user *)__xn_reg_arg2(regs),
				     sizeof(value)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&interval, (void __user *)__xn_reg_arg3(regs),
				     sizeof(interval)))
		return -EFAULT;

	return rt_alarm_start(alarm, value, interval);
}

/*
 * int __rt_alarm_stop(RT_ALARM_PLACEHOLDER *ph)
 */

static int __rt_alarm_stop(struct pt_regs *regs)
{
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM *alarm;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				sizeof(ph)))
		return -EFAULT;

	alarm = (RT_ALARM *)xnregistry_fetch(ph.opaque);

	if (!alarm)
		return -ESRCH;

	return rt_alarm_stop(alarm);
}

/*
 * int __rt_alarm_wait(RT_ALARM_PLACEHOLDER *ph)
 */

static int __rt_alarm_wait(struct pt_regs *regs)
{
	xnthread_t *thread = xnpod_current_thread();
	union xnsched_policy_param param;
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM *alarm;
	xnflags_t info;
	int err = 0;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	alarm =
	    xeno_h2obj_validate(xnregistry_fetch(ph.opaque), XENO_ALARM_MAGIC,
				RT_ALARM);

	if (!alarm) {
		err = xeno_handle_error(alarm, XENO_ALARM_MAGIC, RT_ALARM);
		goto unlock_and_exit;
	}

	if (xnthread_base_priority(thread) != XNSCHED_IRQ_PRIO) {
		/* Boost the waiter above all regular tasks if needed. */
		param.rt.prio = XNSCHED_IRQ_PRIO;
		xnpod_set_thread_schedparam(thread, &xnsched_class_rt, &param);
	}

	info = xnsynch_sleep_on(&alarm->synch_base, XN_INFINITE, XN_RELATIVE);
	if (info & XNRMID)
		err = -EIDRM;	/* Alarm deleted while pending. */
	else if (info & XNBREAK)
		err = -EINTR;	/* Unblocked. */

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/*
 * int __rt_alarm_inquire(RT_ALARM_PLACEHOLDER *ph,
 *                        RT_ALARM_INFO *infop)
 */

static int __rt_alarm_inquire(struct pt_regs *regs)
{
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM_INFO info;
	RT_ALARM *alarm;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	alarm = (RT_ALARM *)xnregistry_fetch(ph.opaque);

	if (!alarm)
		return -ESRCH;

	err = rt_alarm_inquire(alarm, &info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

#else /* !CONFIG_XENO_OPT_NATIVE_ALARM */

#define __rt_alarm_create     __rt_call_not_available
#define __rt_alarm_delete     __rt_call_not_available
#define __rt_alarm_start      __rt_call_not_available
#define __rt_alarm_stop       __rt_call_not_available
#define __rt_alarm_wait       __rt_call_not_available
#define __rt_alarm_inquire    __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_ALARM */

#ifdef CONFIG_XENO_OPT_NATIVE_INTR

int rt_intr_handler(xnintr_t *cookie)
{
	RT_INTR *intr = I_DESC(cookie);

	++intr->pending;

	if (xnsynch_nsleepers(&intr->synch_base) > 0)
		xnsynch_flush(&intr->synch_base, 0);

	if (intr->mode & XN_ISR_PROPAGATE)
		return XN_ISR_PROPAGATE | (intr->mode & XN_ISR_NOENABLE);

	return XN_ISR_HANDLED | (intr->mode & XN_ISR_NOENABLE);
}

EXPORT_SYMBOL_GPL(rt_intr_handler);

/*
 * int __rt_intr_create(RT_INTR_PLACEHOLDER *ph,
 *			const char *name,
 *                      unsigned irq,
 *                      int mode)
 */

static int __rt_intr_create(struct pt_regs *regs)
{
	struct task_struct *p = current;
	char name[XNOBJECT_NAME_LEN];
	RT_INTR_PLACEHOLDER ph;
	int err, mode;
	RT_INTR *intr;
	unsigned irq;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	/* Interrupt line number. */
	irq = (unsigned)__xn_reg_arg3(regs);

	/* Interrupt control mode. */
	mode = (int)__xn_reg_arg4(regs);

	if (mode & ~(I_NOAUTOENA | I_PROPAGATE))
		return -EINVAL;

	intr = (RT_INTR *)xnmalloc(sizeof(*intr));

	if (!intr)
		return -ENOMEM;

	err = rt_intr_create(intr, name, irq, &rt_intr_handler, NULL, 0);

	if (likely(err == 0)) {
		intr->mode = mode;
		intr->cpid = p->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = intr->handle;
		if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
					   sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(intr);

	return err;
}

/*
 * int __rt_intr_bind(RT_INTR_PLACEHOLDER *ph,
 *                    const char *name,
 *                    RTIME *timeoutp)
 */

static int __rt_intr_bind(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_INTR_PLACEHOLDER ph;
	int err;

	err = __rt_bind_helper(p, regs, &ph.opaque, XENO_INTR_MAGIC, NULL, 0);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
				   sizeof(ph)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_intr_delete(RT_INTR_PLACEHOLDER *ph)
 */

static int __rt_intr_delete(struct pt_regs *regs)
{
	RT_INTR_PLACEHOLDER ph;
	RT_INTR *intr;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	intr = (RT_INTR *)xnregistry_fetch(ph.opaque);

	if (!intr)
		return -ESRCH;

	err = rt_intr_delete(intr);

	if (!err && intr->cpid)
		xnfree(intr);

	return err;
}

/*
 * int __rt_intr_wait(RT_INTR_PLACEHOLDER *ph,
 *                    RTIME *timeoutp)
 */

static int __rt_intr_wait(struct pt_regs *regs)
{
	union xnsched_policy_param param;
	RT_INTR_PLACEHOLDER ph;
	xnthread_t *thread;
	xnflags_t info;
	RTIME timeout;
	RT_INTR *intr;
	int err = 0;
	spl_t s;

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg2(regs),
				     sizeof(timeout)))
		return -EFAULT;

	if (timeout == TM_NONBLOCK)
		return -EINVAL;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	intr =
	    xeno_h2obj_validate(xnregistry_fetch(ph.opaque), XENO_INTR_MAGIC,
				RT_INTR);

	if (!intr) {
		err = xeno_handle_error(intr, XENO_INTR_MAGIC, RT_INTR);
		goto unlock_and_exit;
	}

	if (!intr->pending) {
		thread = xnpod_current_thread();

		if (xnthread_base_priority(thread) != XNSCHED_IRQ_PRIO) {
			/* Boost the waiter above all regular tasks if needed. */
			param.rt.prio = XNSCHED_IRQ_PRIO;
			xnpod_set_thread_schedparam(thread, &xnsched_class_rt, &param);
		}

		info = xnsynch_sleep_on(&intr->synch_base,
					timeout, XN_RELATIVE);
		if (info & XNRMID)
			err = -EIDRM;	/* Interrupt object deleted while pending. */
		else if (info & XNTIMEO)
			err = -ETIMEDOUT;	/* Timeout. */
		else if (info & XNBREAK)
			err = -EINTR;	/* Unblocked. */
		else
			err = intr->pending;
	} else
		err = intr->pending;

	intr->pending = 0;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/*
 * int __rt_intr_enable(RT_INTR_PLACEHOLDER *ph)
 */

static int __rt_intr_enable(struct pt_regs *regs)
{
	RT_INTR_PLACEHOLDER ph;
	RT_INTR *intr;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	intr = (RT_INTR *)xnregistry_fetch(ph.opaque);

	if (!intr)
		return -ESRCH;

	return rt_intr_enable(intr);
}

/*
 * int __rt_intr_disable(RT_INTR_PLACEHOLDER *ph)
 */

static int __rt_intr_disable(struct pt_regs *regs)
{
	RT_INTR_PLACEHOLDER ph;
	RT_INTR *intr;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	intr = (RT_INTR *)xnregistry_fetch(ph.opaque);

	if (!intr)
		return -ESRCH;

	return rt_intr_disable(intr);
}

/*
 * int __rt_intr_inquire(RT_INTR_PLACEHOLDER *ph,
 *                       RT_INTR_INFO *infop)
 */

static int __rt_intr_inquire(struct pt_regs *regs)
{
	RT_INTR_PLACEHOLDER ph;
	RT_INTR_INFO info;
	RT_INTR *intr;
	int err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	intr = (RT_INTR *)xnregistry_fetch(ph.opaque);

	if (!intr)
		return -ESRCH;

	err = rt_intr_inquire(intr, &info);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

#else /* !CONFIG_XENO_OPT_NATIVE_INTR */

#define __rt_intr_create     __rt_call_not_available
#define __rt_intr_bind       __rt_call_not_available
#define __rt_intr_delete     __rt_call_not_available
#define __rt_intr_wait       __rt_call_not_available
#define __rt_intr_enable     __rt_call_not_available
#define __rt_intr_disable    __rt_call_not_available
#define __rt_intr_inquire    __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_INTR */

#ifdef CONFIG_XENO_OPT_NATIVE_PIPE

/*
 * int __rt_pipe_create(RT_PIPE_PLACEHOLDER *ph,
 *                      const char *name,
 *                      int minor,
 *                      size_t poolsize)
 */

static int __rt_pipe_create(struct pt_regs *regs)
{
	struct task_struct *p = current;
	char name[XNOBJECT_NAME_LEN];
	RT_PIPE_PLACEHOLDER ph;
	int err, minor;
	size_t poolsize;
	RT_PIPE *pipe;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	/* Device minor. */
	minor = (int)__xn_reg_arg3(regs);

	/* Buffer pool size. */
	poolsize = (size_t) __xn_reg_arg4(regs);

	pipe = (RT_PIPE *)xnmalloc(sizeof(*pipe));

	if (!pipe)
		return -ENOMEM;

	err = rt_pipe_create(pipe, name, minor, poolsize);

	if (likely(err == 0)) {
		pipe->cpid = p->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = pipe->handle;
		if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
					   sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(pipe);

	return err;
}

/*
 * int __rt_pipe_bind(RT_PIPE_PLACEHOLDER *ph,
 *                    const char *name,
 *                    RTIME *timeoutp)
 */

static int __rt_pipe_bind(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_PIPE_PLACEHOLDER ph;
	int err;

	err = __rt_bind_helper(p, regs, &ph.opaque, XENO_PIPE_MAGIC, NULL, 0);

	if (err)
		return err;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
				   sizeof(ph)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_pipe_delete(RT_PIPE_PLACEHOLDER *ph)
 */

static int __rt_pipe_delete(struct pt_regs *regs)
{
	RT_PIPE_PLACEHOLDER ph;
	RT_PIPE *pipe;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	pipe = xnregistry_fetch(ph.opaque);
	if (pipe == NULL)
		return -ESRCH;

	return rt_pipe_delete(pipe);
}

/*
 * int __rt_pipe_read(RT_PIPE_PLACEHOLDER *ph,
 *                    void *buf,
 *                    size_t size,
 *                    RTIME timeout)
 */

static int __rt_pipe_read(struct pt_regs *regs)
{
	RT_PIPE_PLACEHOLDER ph;
	RT_PIPE_MSG *msg;
	RT_PIPE *pipe;
	RTIME timeout;
	size_t size;
	ssize_t err;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	pipe = (RT_PIPE *)xnregistry_fetch(ph.opaque);

	if (!pipe)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg4(regs),
				     sizeof(timeout)))
		return -EFAULT;

	size = (size_t) __xn_reg_arg3(regs);

	err = rt_pipe_receive(pipe, &msg, timeout);

	if (err < 0)
		return err;

	if (msg == NULL)	/* Closed by peer? */
		return 0;

	if (size < P_MSGSIZE(msg))
		err = -ENOBUFS;
	else if (P_MSGSIZE(msg) > 0 &&
		 __xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
					P_MSGPTR(msg), P_MSGSIZE(msg)))
		err = -EFAULT;

	/* Zero-sized messages are allowed, so we still need to free the
	   message buffer even if no data copy took place. */

	rt_pipe_free(pipe, msg);

	return err;
}

/*
 * int __rt_pipe_write(RT_PIPE_PLACEHOLDER *ph,
 *                     const void *buf,
 *                     size_t size,
 *                     int mode)
 */

static int __rt_pipe_write(struct pt_regs *regs)
{
	RT_PIPE_PLACEHOLDER ph;
	RT_PIPE_MSG *msg;
	RT_PIPE *pipe;
	size_t size;
	ssize_t err;
	int mode;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	pipe = (RT_PIPE *)xnregistry_fetch(ph.opaque);

	if (!pipe)
		return -ESRCH;

	size = (size_t) __xn_reg_arg3(regs);
	mode = (int)__xn_reg_arg4(regs);

	if (size == 0)
		/* Try flushing the streaming buffer in any case. */
		return rt_pipe_send(pipe, NULL, 0, mode);

	msg = rt_pipe_alloc(pipe, size);

	if (!msg)
		return -ENOMEM;

	if (__xn_safe_copy_from_user(P_MSGPTR(msg),
				     (void __user *)__xn_reg_arg2(regs), size)) {
		rt_pipe_free(pipe, msg);
		return -EFAULT;
	}

	err = rt_pipe_send(pipe, msg, size, mode);

	if (err != size)
		/* If the operation failed, we need to free the message buffer
		   by ourselves. */
		rt_pipe_free(pipe, msg);

	return err;
}

/*
 * int __rt_pipe_stream(RT_PIPE_PLACEHOLDER *ph,
 *                      const void *buf,
 *                      size_t size)
 */

static int __rt_pipe_stream(struct pt_regs *regs)
{
	RT_PIPE_PLACEHOLDER ph;
	RT_PIPE_MSG *msg;
	char tmp_buf[64];
	RT_PIPE *pipe;
	size_t size;
	ssize_t err;
	void *buf;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	pipe = (RT_PIPE *)xnregistry_fetch(ph.opaque);

	if (!pipe)
		return -ESRCH;

	size = (size_t) __xn_reg_arg3(regs);

	if (size == 0)
		/* Try flushing the streaming buffer in any case. */
		return rt_pipe_stream(pipe, NULL, 0);

	/* Try using a local fast buffer if the sent data fits into it. */

	if (size <= sizeof(tmp_buf)) {
		msg = NULL;
		buf = tmp_buf;
	} else {
		msg = rt_pipe_alloc(pipe, size);

		if (!msg)
			return -ENOMEM;

		buf = P_MSGPTR(msg);
	}

	if (__xn_safe_copy_from_user(buf, (void __user *)__xn_reg_arg2(regs), size)) {
		err = -EFAULT;
		goto out;
	}

	err = rt_pipe_stream(pipe, buf, size);

out:
	if (msg)
		rt_pipe_free(pipe, msg);

	return err;
}

#else /* !CONFIG_XENO_OPT_NATIVE_PIPE */

#define __rt_pipe_create   __rt_call_not_available
#define __rt_pipe_bind     __rt_call_not_available
#define __rt_pipe_delete   __rt_call_not_available
#define __rt_pipe_read     __rt_call_not_available
#define __rt_pipe_write    __rt_call_not_available
#define __rt_pipe_stream   __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_PIPE */

#ifdef CONFIG_XENO_OPT_NATIVE_BUFFER

/*
 * int __rt_buffer_create(RT_BUFFER_PLACEHOLDER *ph,
 *                        const char *name,
 *                        size_t bufsz,
 *                        int mode)
 */

static int __rt_buffer_create(struct pt_regs *regs)
{
	char name[XNOBJECT_NAME_LEN];
	RT_BUFFER_PLACEHOLDER ph;
	RT_BUFFER *bf;
	int ret, mode;
	size_t bufsz;

	if (__xn_reg_arg2(regs)) {
		if (__xn_safe_strncpy_from_user(name,
						(const char __user *)__xn_reg_arg2(regs),
						sizeof(name) - 1) < 0)
			return -EFAULT;
		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	/* Buffer size. */
	bufsz = __xn_reg_arg3(regs);
	/* Creation mode. */
	mode = __xn_reg_arg4(regs);

	bf = xnmalloc(sizeof(*bf));
	if (!bf)
		return -ENOMEM;

	ret = rt_buffer_create(bf, name, bufsz, mode);
	if (ret == 0) {
		bf->cpid = current->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = bf->handle;
		if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
					   sizeof(ph)))
			ret = -EFAULT;
	} else
		xnfree(bf);

	return ret;
}

/*
 * int __rt_buffer_bind(RT_BUFFER_PLACEHOLDER *ph,
 *                      const char *name,
 *                      RTIME *timeoutp)
 */

static int __rt_buffer_bind(struct pt_regs *regs)
{
	RT_BUFFER_PLACEHOLDER ph;
	int ret;

	ret =
	    __rt_bind_helper(current, regs, &ph.opaque, XENO_BUFFER_MAGIC,
			     NULL, 0);
	if (ret)
		return ret;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph,
				   sizeof(ph)))
		return -EFAULT;

	return 0;
}

/*
 * int __rt_buffer_delete(RT_BUFFER_PLACEHOLDER *ph)
 */

static int __rt_buffer_delete(struct pt_regs *regs)
{
	RT_BUFFER_PLACEHOLDER ph;
	RT_BUFFER *bf;
	int ret;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	bf = xnregistry_fetch(ph.opaque);
	if (!bf)
		return -ESRCH;

	ret = rt_buffer_delete(bf);
	if (ret == 0 && bf->cpid)
		xnfree(bf);

	return ret;
}

/*
 * int __rt_buffer_write(RT_BUFFER_PLACEHOLDER *ph,
 *                       const void *buf,
 *                       size_t size,
 *                       xntmode_t timeout_mode,
 *                       RTIME *timeoutp)
 */

static int __rt_buffer_write(struct pt_regs *regs)
{
	RT_BUFFER_PLACEHOLDER ph;
	xntmode_t timeout_mode;
	struct xnbufd bufd;
	void __user *ptr;
	RTIME timeout;
	RT_BUFFER *bf;
	size_t size;
	ssize_t ret;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg5(regs),
				     sizeof(timeout)))
		return -EFAULT;

	ptr = (void __user *)__xn_reg_arg2(regs);
	size = __xn_reg_arg3(regs);
	timeout_mode = __xn_reg_arg4(regs);

	bf = xnregistry_fetch(ph.opaque);
	if (bf == NULL)
		return -ESRCH;

	xnbufd_map_uread(&bufd, ptr, size);
	ret = rt_buffer_write_inner(bf, &bufd, timeout_mode, timeout);
	xnbufd_unmap_uread(&bufd);

	return ret;
}

/*
 * int __rt_buffer_read(RT_BUFFER_PLACEHOLDER *ph,
 *                      void *buf,
 *                      size_t size,
 *                      xntmode_t timeout_mode,
 *                      RTIME *timeoutp)
 */

static int __rt_buffer_read(struct pt_regs *regs)
{
	RT_BUFFER_PLACEHOLDER ph;
	xntmode_t timeout_mode;
	struct xnbufd bufd;
	void __user *ptr;
	RTIME timeout;
	RT_BUFFER *bf;
	size_t size;
	ssize_t ret;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, (void __user *)__xn_reg_arg5(regs),
				     sizeof(timeout)))
		return -EFAULT;

	ptr = (void __user *)__xn_reg_arg2(regs);
	size = __xn_reg_arg3(regs);
	timeout_mode = __xn_reg_arg4(regs);

	bf = xnregistry_fetch(ph.opaque);
	if (bf == NULL)
		return -ESRCH;

	xnbufd_map_uwrite(&bufd, ptr, size);
	ret = rt_buffer_read_inner(bf, &bufd, timeout_mode, timeout);
	xnbufd_unmap_uwrite(&bufd);

	return ret;
}

/*
 * int __rt_buffer_clear(RT_BUFFER_PLACEHOLDER *ph)
 */

static int __rt_buffer_clear(struct pt_regs *regs)
{
	RT_BUFFER_PLACEHOLDER ph;
	RT_BUFFER *bf;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	bf = xnregistry_fetch(ph.opaque);
	if (bf == NULL)
		return -ESRCH;

	return rt_buffer_clear(bf);
}

/*
 * int __rt_buffer_inquire(RT_BUFFER_PLACEHOLDER *ph,
 *                         RT_BUFFER_INFO *infop)
 */

static int __rt_buffer_inquire(struct pt_regs *regs)
{
	RT_BUFFER_PLACEHOLDER ph;
	RT_BUFFER_INFO info;
	RT_BUFFER *bf;
	int ret;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	bf = xnregistry_fetch(ph.opaque);
	if (bf == NULL)
		return -ESRCH;

	ret = rt_buffer_inquire(bf, &info);
	if (ret)
		return ret;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg2(regs),
				   &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

#else /* !CONFIG_XENO_OPT_NATIVE_BUFFER */

#define __rt_buffer_create   __rt_call_not_available
#define __rt_buffer_bind     __rt_call_not_available
#define __rt_buffer_delete   __rt_call_not_available
#define __rt_buffer_read     __rt_call_not_available
#define __rt_buffer_write    __rt_call_not_available
#define __rt_buffer_clear    __rt_call_not_available
#define __rt_buffer_inquire  __rt_call_not_available

#endif /* !CONFIG_XENO_OPT_NATIVE_BUFFER */

/*
 * int __rt_io_get_region(RT_IOREGION_PLACEHOLDER *ph,
 *                        const char *name,
 *                        uint64_t *startp,
 *                        uint64_t *lenp,
 *                        int flags)
 */

static int __rt_io_get_region(struct pt_regs *regs)
{
	struct task_struct *p = current;
	RT_IOREGION_PLACEHOLDER ph;
	uint64_t start, len;
	RT_IOREGION *iorn;
	int err, flags;
	spl_t s;

	iorn = (RT_IOREGION *) xnmalloc(sizeof(*iorn));

	if (!iorn)
		return -ENOMEM;

	if (__xn_safe_strncpy_from_user(iorn->name,
					(const char __user *)__xn_reg_arg2(regs),
					sizeof(iorn->name) - 1) < 0)
		return -EFAULT;

	iorn->name[sizeof(iorn->name) - 1] = '\0';

	err = xnregistry_enter(iorn->name, iorn, &iorn->handle, NULL);

	if (err)
		goto fail;

	if (__xn_safe_copy_from_user(&start, (void __user *)__xn_reg_arg3(regs),
				     sizeof(start))) {
		err = -EFAULT;
		goto fail;
	}

	if (__xn_safe_copy_from_user(&len, (void __user *)__xn_reg_arg4(regs),
				     sizeof(len))) {
		err = -EFAULT;
		goto fail;
	}

	flags = __xn_reg_arg5(regs);

	if (flags & IORN_IOPORT)
		err = request_region(start, len, iorn->name) ? 0 : -EBUSY;
	else if (flags & IORN_IOMEM)
		err = request_mem_region(start, len, iorn->name) ? 0 : -EBUSY;
	else
		err = -EINVAL;

	if (unlikely(err != 0))
		goto fail;

	iorn->magic = XENO_IOREGION_MAGIC;
	iorn->start = start;
	iorn->len = len;
	iorn->flags = flags;
	inith(&iorn->rlink);
	iorn->rqueue = &xeno_get_rholder()->ioregionq;
	xnlock_get_irqsave(&nklock, s);
	appendq(iorn->rqueue, &iorn->rlink);
	xnlock_put_irqrestore(&nklock, s);
	iorn->cpid = p->pid;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = iorn->handle;

	if (__xn_safe_copy_to_user((void __user *)__xn_reg_arg1(regs), &ph, sizeof(ph)))
		return -EFAULT;

	return 0;

      fail:
	xnfree(iorn);

	return err;
}

/* Provided for auto-cleanup support. */
int rt_ioregion_delete(RT_IOREGION * iorn)
{
	uint64_t start, len;
	int flags;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	flags = iorn->flags;
	start = iorn->start;
	len = iorn->len;
	removeq(iorn->rqueue, &iorn->rlink);
	xnregistry_remove(iorn->handle);

	xnlock_put_irqrestore(&nklock, s);

	if (flags & IORN_IOPORT)
		release_region(start, len);
	else if (flags & IORN_IOMEM)
		release_mem_region(start, len);

	return 0;
}

/*
 * int __rt_io_put_region(RT_IOREGION_PLACEHOLDER *ph)
 */

static int __rt_io_put_region(struct pt_regs *regs)
{
	RT_IOREGION_PLACEHOLDER ph;
	uint64_t start, len;
	RT_IOREGION *iorn;
	int flags;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, (void __user *)__xn_reg_arg1(regs),
				     sizeof(ph)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	iorn = (RT_IOREGION *) xnregistry_fetch(ph.opaque);
	if (iorn == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return -ESRCH;
	}

	flags = iorn->flags;
	start = iorn->start;
	len = iorn->len;
	removeq(iorn->rqueue, &iorn->rlink);
	xnregistry_remove(iorn->handle);

	xnlock_put_irqrestore(&nklock, s);

	xnfree(iorn);

	if (flags & IORN_IOPORT)
		release_region(start, len);
	else if (flags & IORN_IOMEM)
		release_mem_region(start, len);

	return 0;
}

static __attribute__ ((unused))
int __rt_call_not_available(struct pt_regs *regs)
{
	return -ENOSYS;
}

static void __shadow_delete_hook(xnthread_t *thread)
{
	if (xnthread_get_magic(thread) == XENO_SKIN_MAGIC &&
	    xnthread_test_state(thread, XNMAPPED))
		xnshadow_unmap(thread);
}

static void *__shadow_eventcb(int event, void *data)
{
	struct xeno_resource_holder *rh;
	switch (event) {

	case XNSHADOW_CLIENT_ATTACH:

		rh = (struct xeno_resource_holder *)
		    xnarch_alloc_host_mem(sizeof(*rh));
		if (!rh)
			return ERR_PTR(-ENOMEM);

		initq(&rh->alarmq);
		initq(&rh->condq);
		initq(&rh->eventq);
		initq(&rh->heapq);
		initq(&rh->intrq);
		initq(&rh->mutexq);
		initq(&rh->pipeq);
		initq(&rh->queueq);
		initq(&rh->semq);
		initq(&rh->ioregionq);
		initq(&rh->bufferq);

		return &rh->ppd;

	case XNSHADOW_CLIENT_DETACH:

		rh = ppd2rholder((xnshadow_ppd_t *) data);
		__native_alarm_flush_rq(&rh->alarmq);
		__native_cond_flush_rq(&rh->condq);
		__native_event_flush_rq(&rh->eventq);
		__native_heap_flush_rq(&rh->heapq);
		__native_intr_flush_rq(&rh->intrq);
		__native_mutex_flush_rq(&rh->mutexq);
		__native_pipe_flush_rq(&rh->pipeq);
		__native_queue_flush_rq(&rh->queueq);
		__native_sem_flush_rq(&rh->semq);
		__native_ioregion_flush_rq(&rh->ioregionq);
		__native_buffer_flush_rq(&rh->bufferq);

		xnarch_free_host_mem(rh, sizeof(*rh));

		return NULL;
	}

	return ERR_PTR(-EINVAL);
}

static xnsysent_t __systab[] = {
	[__native_task_create] = {&__rt_task_create, __xn_exec_init},
	[__native_task_bind] = {&__rt_task_bind, __xn_exec_conforming},
	[__native_task_start] = {&__rt_task_start, __xn_exec_any},
	[__native_task_suspend] = {&__rt_task_suspend, __xn_exec_conforming},
	[__native_task_resume] = {&__rt_task_resume, __xn_exec_any},
	[__native_task_delete] = {&__rt_task_delete, __xn_exec_conforming},
	[__native_task_yield] = {&__rt_task_yield, __xn_exec_primary},
	[__native_task_set_periodic] =
	    {&__rt_task_set_periodic, __xn_exec_conforming},
	[__native_task_wait_period] =
	    {&__rt_task_wait_period, __xn_exec_primary},
	[__native_task_set_priority] = {&__rt_task_set_priority, __xn_exec_any},
	[__native_task_sleep] = {&__rt_task_sleep, __xn_exec_primary},
	[__native_task_sleep_until] =
	    {&__rt_task_sleep_until, __xn_exec_primary},
	[__native_task_unblock] = {&__rt_task_unblock, __xn_exec_any},
	[__native_task_inquire] = {&__rt_task_inquire, __xn_exec_any},
	[__native_task_notify] = {&__rt_task_notify, __xn_exec_any},
	[__native_task_set_mode] = {&__rt_task_set_mode, __xn_exec_primary},
	[__native_task_self] = {&__rt_task_self, __xn_exec_any},
	[__native_task_slice] = {&__rt_task_slice, __xn_exec_any},
	[__native_task_send] = {&__rt_task_send, __xn_exec_primary},
	[__native_task_receive] = {&__rt_task_receive, __xn_exec_primary},
	[__native_task_reply] = {&__rt_task_reply, __xn_exec_primary},
	[__native_timer_set_mode] =
	    {&__rt_timer_set_mode, __xn_exec_lostage | __xn_exec_switchback},
	[__native_unimp_22] = {&__rt_call_not_available, __xn_exec_any},
	[__native_timer_read] = {&__rt_timer_read, __xn_exec_any},
	[__native_timer_tsc] = {&__rt_timer_tsc, __xn_exec_any},
	[__native_timer_ns2ticks] = {&__rt_timer_ns2ticks, __xn_exec_any},
	[__native_timer_ticks2ns] = {&__rt_timer_ticks2ns, __xn_exec_any},
	[__native_timer_inquire] = {&__rt_timer_inquire, __xn_exec_any},
	[__native_timer_spin] = {&__rt_timer_spin, __xn_exec_any},
	[__native_sem_create] = {&__rt_sem_create, __xn_exec_any},
	[__native_sem_bind] = {&__rt_sem_bind, __xn_exec_conforming},
	[__native_sem_delete] = {&__rt_sem_delete, __xn_exec_any},
	[__native_sem_p] = {&__rt_sem_p, __xn_exec_primary},
	[__native_sem_v] = {&__rt_sem_v, __xn_exec_any},
	[__native_sem_broadcast] = {&__rt_sem_broadcast, __xn_exec_any},
	[__native_sem_inquire] = {&__rt_sem_inquire, __xn_exec_any},
	[__native_event_create] = {&__rt_event_create, __xn_exec_any},
	[__native_event_bind] = {&__rt_event_bind, __xn_exec_conforming},
	[__native_event_delete] = {&__rt_event_delete, __xn_exec_any},
	[__native_event_wait] = {&__rt_event_wait, __xn_exec_primary},
	[__native_event_signal] = {&__rt_event_signal, __xn_exec_any},
	[__native_event_clear] = {&__rt_event_clear, __xn_exec_any},
	[__native_event_inquire] = {&__rt_event_inquire, __xn_exec_any},
	[__native_mutex_create] = {&__rt_mutex_create, __xn_exec_any},
	[__native_mutex_bind] = {&__rt_mutex_bind, __xn_exec_conforming},
	[__native_mutex_delete] = {&__rt_mutex_delete, __xn_exec_any},
	[__native_mutex_acquire] = {&__rt_mutex_acquire, __xn_exec_primary},
	[__native_mutex_release] = {&__rt_mutex_release, __xn_exec_primary},
	[__native_mutex_inquire] = {&__rt_mutex_inquire, __xn_exec_any},
	[__native_cond_create] = {&__rt_cond_create, __xn_exec_any},
	[__native_cond_bind] = {&__rt_cond_bind, __xn_exec_conforming},
	[__native_cond_delete] = {&__rt_cond_delete, __xn_exec_any},
	[__native_cond_wait_prologue] =
		{&__rt_cond_wait_prologue,
		 __xn_exec_primary | __xn_exec_norestart},
	[__native_cond_wait_epilogue] =
		{&__rt_cond_wait_epilogue, __xn_exec_primary},
	[__native_cond_signal] = {&__rt_cond_signal, __xn_exec_any},
	[__native_cond_broadcast] = {&__rt_cond_broadcast, __xn_exec_any},
	[__native_cond_inquire] = {&__rt_cond_inquire, __xn_exec_any},
	[__native_queue_create] = {&__rt_queue_create, __xn_exec_lostage},
	[__native_queue_bind] = {&__rt_queue_bind, __xn_exec_conforming},
	[__native_queue_delete] = {&__rt_queue_delete, __xn_exec_lostage},
	[__native_queue_alloc] = {&__rt_queue_alloc, __xn_exec_any},
	[__native_queue_free] = {&__rt_queue_free, __xn_exec_any},
	[__native_queue_send] = {&__rt_queue_send, __xn_exec_any},
	[__native_queue_write] = {&__rt_queue_write, __xn_exec_any},
	[__native_queue_receive] = {&__rt_queue_receive, __xn_exec_primary},
	[__native_queue_read] = {&__rt_queue_read, __xn_exec_primary},
	[__native_queue_inquire] = {&__rt_queue_inquire, __xn_exec_any},
	[__native_queue_flush] = {&__rt_queue_flush, __xn_exec_any},
	[__native_heap_create] = {&__rt_heap_create, __xn_exec_lostage},
	[__native_heap_bind] = {&__rt_heap_bind, __xn_exec_conforming},
	[__native_heap_delete] = {&__rt_heap_delete, __xn_exec_lostage},
	[__native_heap_alloc] = {&__rt_heap_alloc, __xn_exec_conforming},
	[__native_heap_free] = {&__rt_heap_free, __xn_exec_any},
	[__native_heap_inquire] = {&__rt_heap_inquire, __xn_exec_any},
	[__native_alarm_create] = {&__rt_alarm_create, __xn_exec_any},
	[__native_alarm_delete] = {&__rt_alarm_delete, __xn_exec_any},
	[__native_alarm_start] = {&__rt_alarm_start, __xn_exec_any},
	[__native_alarm_stop] = {&__rt_alarm_stop, __xn_exec_any},
	[__native_alarm_wait] = {&__rt_alarm_wait, __xn_exec_primary},
	[__native_alarm_inquire] = {&__rt_alarm_inquire, __xn_exec_any},
	[__native_intr_create] = {&__rt_intr_create, __xn_exec_any},
	[__native_intr_bind] = {&__rt_intr_bind, __xn_exec_conforming},
	[__native_intr_delete] = {&__rt_intr_delete, __xn_exec_any},
	[__native_intr_wait] = {&__rt_intr_wait, __xn_exec_primary},
	[__native_intr_enable] = {&__rt_intr_enable, __xn_exec_any},
	[__native_intr_disable] = {&__rt_intr_disable, __xn_exec_any},
	[__native_intr_inquire] = {&__rt_intr_inquire, __xn_exec_any},
	[__native_pipe_create] = {&__rt_pipe_create, __xn_exec_lostage},
	[__native_pipe_bind] = {&__rt_pipe_bind, __xn_exec_conforming},
	[__native_pipe_delete] = {&__rt_pipe_delete, __xn_exec_lostage},
	[__native_pipe_read] = {&__rt_pipe_read, __xn_exec_primary},
	[__native_pipe_write] = {&__rt_pipe_write, __xn_exec_any},
	[__native_pipe_stream] = {&__rt_pipe_stream, __xn_exec_any},
	[__native_unimp_89] = {&__rt_call_not_available, __xn_exec_any},
	[__native_io_get_region] = {&__rt_io_get_region, __xn_exec_lostage},
	[__native_io_put_region] = {&__rt_io_put_region, __xn_exec_lostage},
	[__native_unimp_92] = {&__rt_call_not_available, __xn_exec_any},
	[__native_unimp_93] = {&__rt_call_not_available, __xn_exec_any},
	[__native_buffer_create] = {&__rt_buffer_create, __xn_exec_lostage},
	[__native_buffer_bind] = {&__rt_buffer_bind, __xn_exec_conforming},
	[__native_buffer_delete] = {&__rt_buffer_delete, __xn_exec_lostage},
	[__native_buffer_read] = {&__rt_buffer_read, __xn_exec_conforming},
	[__native_buffer_write] = {&__rt_buffer_write, __xn_exec_conforming},
	[__native_buffer_clear] = {&__rt_buffer_clear, __xn_exec_any},
	[__native_buffer_inquire] = {&__rt_buffer_inquire, __xn_exec_any},
};

static struct xnskin_props __props = {
	.name = "native",
	.magic = XENO_SKIN_MAGIC,
	.nrcalls = sizeof(__systab) / sizeof(__systab[0]),
	.systab = __systab,
	.eventcb = &__shadow_eventcb,
	.timebasep = &__native_tbase,
	.module = THIS_MODULE
};

int __native_syscall_init(void)
{
	__native_muxid = xnshadow_register_interface(&__props);

	if (__native_muxid < 0)
		return -ENOSYS;

	xnpod_add_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);

	return 0;
}

void __native_syscall_cleanup(void)
{
	xnpod_remove_hook(XNHOOK_THREAD_DELETE, &__shadow_delete_hook);
	xnshadow_unregister_interface(__native_muxid);
}
