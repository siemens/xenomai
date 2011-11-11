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
#include <native/buffer.h>

#define rt_task_errno (*xnthread_get_errno_location(xnpod_current_thread()))

/*
 * This file implements the Xenomai syscall wrappers.  All skin
 * services (re-)check the object descriptor they are passed; so there
 * may be no race between a call to xnregistry_fetch() where the
 * user-space handle is converted to a descriptor pointer, and the use
 * of it in the actual syscall.
 */

int __native_muxid;

static int __rt_bind_helper(const char __user *u_name,
			    RTIME __user *u_timeout,
			    xnhandle_t *handlep,
			    unsigned magic, void **objaddrp,
			    unsigned long objoffs)
{
	char name[XNOBJECT_NAME_LEN];
	RTIME timeout;
	void *objaddr;
	spl_t s;
	int err;

	if (__xn_safe_strncpy_from_user(name, u_name,
					sizeof(name) - 1) < 0)
		return -EFAULT;

	name[sizeof(name) - 1] = '\0';

	if (__xn_safe_copy_from_user(&timeout, u_timeout,
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

static int __rt_task_create(RT_TASK_PLACEHOLDER __user *u_ph,
			    const char __user *u_name,
			    int prio,
			    int mode,
			    struct native_hidden_desc __user *u_desc)
{
	struct task_struct *p = current;
	struct native_hidden_desc desc;
	char name[XNOBJECT_NAME_LEN];
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task = NULL;
	int err;

	if (__xn_safe_copy_from_user(&desc, u_desc, sizeof(desc)))
		return -EFAULT;

	if (xnshadow_thread(p)) {
		err = -EBUSY;
		goto fail;
	}

	if (u_name) {
		if (__xn_safe_strncpy_from_user(name, u_name,
						sizeof(name) - 1) < 0) {
			err = -EFAULT;
			goto fail;
		}
		name[sizeof(name) - 1] = '\0';
		strncpy(p->comm, name, sizeof(p->comm));
		p->comm[sizeof(p->comm) - 1] = '\0';
	} else
		*name = '\0';

	task = xnmalloc(sizeof(*task));
	if (task == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	xnthread_clear_state(&task->thread_base, XNZOMBIE);

	/*
	 * Force FPU support in user-space. This will lead to a no-op
	 * if the platform does not support it.
	 */
	err = rt_task_create(task, name, 0, prio,
			     XNFPU | XNSHADOW | (mode & (T_CPUMASK | T_SUSP)));
	if (err) {
		task = NULL;
		goto fail;
	}

	/* Apply CPU affinity */
	set_cpus_allowed(p, task->affinity);

	/* Copy back the registry handle to the ph struct. */
	ph.opaque = xnthread_handle(&task->thread_base);
	ph.opaque2 = desc.opaque_handle; /* hidden pthread_t identifier. */
	if (__xn_safe_copy_to_user(u_ph, &ph, sizeof(ph))) {
		err = -EFAULT;
		goto delete;
	}

	if (desc.writeback == NULL) {
		err = -ENOMEM;
		goto delete;
	}

	err = xnshadow_map(&task->thread_base, desc.completion,
			   (unsigned long __user *)desc.writeback);
	if (err)
		goto delete;

	if (mode & T_WARNSW)
		xnpod_set_thread_mode(&task->thread_base, 0, XNTRAPSW);

	return 0;

delete:
	rt_task_delete(task);

fail:
	/* Unblock and pass back error code. */
	if (desc.completion)
		xnshadow_signal_completion(desc.completion, err);

	/*
	 * Task memory could have been released by an indirect call to
	 * the deletion hook, after xnpod_delete_thread() has been
	 * issued from rt_task_create() (e.g. upon registration
	 * error). We avoid double memory release when the XNZOMBIE
	 * flag is raised, meaning the deletion hook has run, and the
	 * TCB memory is already scheduled for release.
	 */
	if (task != NULL
	    && !xnthread_test_state(&task->thread_base, XNZOMBIE))
		xnfree(task);

	return err;
}

static int __rt_task_bind(RT_TASK_PLACEHOLDER __user *u_ph,
			  const char __user *u_name,
			  RTIME __user *u_timeout)
{
	RT_TASK_PLACEHOLDER ph;
	int ret;

	ret = __rt_bind_helper(u_name, u_timeout,
			       &ph.opaque, XENO_TASK_MAGIC, NULL,
			       -offsetof(RT_TASK, thread_base));
	if (ret)
		return ret;

	/*
	 * We just don't know the associated user-space pthread
	 * identifier -- clear it to prevent misuse.
	 */
	ph.opaque2 = 0;

	return __xn_safe_copy_to_user(u_ph, &ph, sizeof(ph));
}

static int __rt_task_start(RT_TASK_PLACEHOLDER __user *u_ph,
			   void __user (*u_entry)(void *cookie),
			   void __user *u_cookie)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	task = __rt_task_lookup(ph.opaque);
	if (task == NULL)
		return -ESRCH;

	return rt_task_start(task,
			     (void (*)(void *))u_entry,
			     (void *)u_cookie);
}

static int __rt_task_suspend(RT_TASK_PLACEHOLDER __user *u_ph)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (u_ph) {
		if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (task == NULL)
		return -ESRCH;

	return rt_task_suspend(task);
}

static int __rt_task_resume(RT_TASK_PLACEHOLDER __user *u_ph)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	task = __rt_task_lookup(ph.opaque);
	if (task == NULL)
		return -ESRCH;

	return rt_task_resume(task);
}

static int __rt_task_delete(RT_TASK_PLACEHOLDER __user *u_ph)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (u_ph) {
		if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (task == NULL)
		return -ESRCH;

	return rt_task_delete(task);	/* TCB freed in delete hook. */
}

static int __rt_task_yield(void)
{
	return rt_task_yield();
}

static int __rt_task_set_periodic(RT_TASK_PLACEHOLDER __user *u_ph,
				  RTIME __user *u_idate,
				  RTIME __user *u_period)
{
	RT_TASK_PLACEHOLDER ph;
	RTIME idate, period;
	RT_TASK *task;

	if (u_ph) {
		if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (task == NULL)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&idate, u_idate, sizeof(idate)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&period, u_period, sizeof(period)))
		return -EFAULT;

	return rt_task_set_periodic(task, idate, period);
}

static int __rt_task_wait_period(unsigned long __user *u_overruns)
{
	unsigned long overruns;
	int ret;

	ret = rt_task_wait_period(&overruns);
	if (u_overruns && (ret == 0 || ret == -ETIMEDOUT))
		if (__xn_safe_copy_to_user(u_overruns,
					   &overruns, sizeof(overruns)))
			ret = -EFAULT;
	return ret;
}

static int __rt_task_set_priority(RT_TASK_PLACEHOLDER __user *u_ph,
				  int prio)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (u_ph) {
		if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (task == NULL)
		return -ESRCH;

	return rt_task_set_priority(task, prio);
}

static int __rt_task_sleep(RTIME __user *u_delay)
{
	RTIME delay;

	if (__xn_safe_copy_from_user(&delay, u_delay, sizeof(delay)))
		return -EFAULT;

	return rt_task_sleep(delay);
}

static int __rt_task_sleep_until(RTIME __user *u_date)
{
	RTIME date;

	if (__xn_safe_copy_from_user(&date, u_date, sizeof(date)))
		return -EFAULT;

	return rt_task_sleep_until(date);
}

static int __rt_task_unblock(RT_TASK_PLACEHOLDER __user *u_ph)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	task = __rt_task_lookup(ph.opaque);
	if (task == NULL)
		return -ESRCH;

	return rt_task_unblock(task);
}

static int __rt_task_inquire(RT_TASK_PLACEHOLDER __user *u_ph,
			     RT_TASK_INFO __user *u_info)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK_INFO info;
	RT_TASK *task;
	int ret;

	if (u_ph) {
		if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (task == NULL)
		return -ESRCH;

	if (unlikely(u_info == NULL))
		/* Probe for existence. */
		return 0;

	ret = rt_task_inquire(task, &info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
}

static int __rt_task_notify(RT_TASK_PLACEHOLDER __user *u_ph,
			    rt_sigset_t signals)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	if (u_ph) {
		if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (task == NULL)
		return -ESRCH;

	return rt_task_notify(task, signals);
}

static int __rt_task_set_mode(int clrmask,
			      int setmask,
			      int __user *u_mode)
{
	int ret, mode_r;

	if (clrmask & T_CONFORMING)
		return -EINVAL;

	/*
	 * This call already required a primary mode switch, so if
	 * T_CONFORMING was specified for a real-time shadow, we are
	 * fine. If it was given from a non real-time shadow, well
	 * this is silly, and we'll be relaxed soon due to the
	 * auto-relax feature, leading to a nop.
	 */
	setmask &= ~T_CONFORMING;
	ret = rt_task_set_mode(clrmask, setmask, &mode_r);
	if (ret)
		return ret;

	mode_r |= T_CONFORMING;

	if (u_mode &&
	    __xn_safe_copy_to_user(u_mode, &mode_r, sizeof(mode_r)))
		return -EFAULT;

	return 0;
}

static int __rt_task_self(RT_TASK_PLACEHOLDER __user *u_ph)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;

	/*
	 * Calls on behalf of a non-task context beget an error for
	 * the user-space interface.
	 */
	task = __rt_task_current(current);
	if (task == NULL)
		return -ESRCH;

	ph.opaque = xnthread_handle(&task->thread_base);

	return __xn_safe_copy_to_user(u_ph, &ph, sizeof(ph));
}

static int __rt_task_slice(RT_TASK_PLACEHOLDER __user *u_ph,
			   RTIME __user *u_quantum)
{
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;
	RTIME quantum;

	if (u_ph) {
		if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (task == NULL)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&quantum, u_quantum, sizeof(quantum)))
		return -EFAULT;

	return rt_task_slice(task, quantum);
}

#ifdef CONFIG_XENO_OPT_NATIVE_MPS

static int __rt_task_send(RT_TASK_PLACEHOLDER __user *u_ph,
			  RT_TASK_MCB __user *u_mcb_s,
			  RT_TASK_MCB __user *u_mcb_r,
			  RTIME __user *u_timeout)
{
	char tmp_buf[RT_MCB_FSTORE_LIMIT];
	RT_TASK_MCB mcb_s, mcb_r;
	caddr_t tmp_area, data_r;
	RT_TASK_PLACEHOLDER ph;
	RT_TASK *task;
	RTIME timeout;
	size_t xsize;
	ssize_t err;

	if (u_ph) {
		if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
			return -EFAULT;

		task = __rt_task_lookup(ph.opaque);
	} else
		task = __rt_task_current(current);

	if (task == NULL)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&mcb_s, u_mcb_s, sizeof(mcb_s)))
		return -EFAULT;

	if (u_mcb_r) {
		if (__xn_safe_copy_from_user(&mcb_r, u_mcb_r, sizeof(mcb_r)))
			return -EFAULT;
	} else {
		mcb_r.data = NULL;
		mcb_r.size = 0;
	}

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	xsize = mcb_s.size + mcb_r.size;
	data_r = mcb_r.data;

	if (xsize > 0) {
		/*
		 * Try optimizing a bit here: if the cumulated message
		 * sizes (initial+reply) can fit into our local
		 * buffer, use it; otherwise, take the slow path and
		 * fetch a larger buffer from the system heap. Most
		 * messages are expected to be short enough to fit on
		 * the stack anyway.
		 */
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

	if (u_mcb_r) {
		mcb_r.data = data_r;
		if (__xn_safe_copy_to_user(u_mcb_r, &mcb_r, sizeof(mcb_r)))
			err = -EFAULT;
	}

out:
	if (tmp_area && tmp_area != tmp_buf)
		xnfree(tmp_area);

	return err;
}

static int __rt_task_receive(RT_TASK_MCB __user *u_mcb_r,
			     RTIME __user *u_timeout)
{
	char tmp_buf[RT_MCB_FSTORE_LIMIT];
	caddr_t tmp_area, data_r;
	RT_TASK_MCB mcb_r;
	RTIME timeout;
	int err;

	if (__xn_safe_copy_from_user(&mcb_r, u_mcb_r, sizeof(mcb_r)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	data_r = mcb_r.data;

	if (mcb_r.size > 0) {
		/*
		 * Same optimization as in __rt_task_send(): if the
		 * size of the reply message can fit into our local
		 * buffer, use it; otherwise, take the slow path and
		 * fetch a larger buffer from the system heap.
		 */
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

	if (__xn_safe_copy_to_user(u_mcb_r, &mcb_r, sizeof(mcb_r)))
		err = -EFAULT;

out:
	if (tmp_area && tmp_area != tmp_buf)
		xnfree(tmp_area);

	return err;
}

static int __rt_task_reply(int flowid,
			   RT_TASK_MCB __user *u_mcb_s)
{
	char tmp_buf[RT_MCB_FSTORE_LIMIT];
	RT_TASK_MCB mcb_s;
	caddr_t tmp_area;
	int err;

	if (u_mcb_s) {
		if (__xn_safe_copy_from_user(&mcb_s, u_mcb_s, sizeof(mcb_s)))
			return -EFAULT;
	} else {
		mcb_s.data = NULL;
		mcb_s.size = 0;
	}

	if (mcb_s.size > 0) {
		/*
		 * Same optimization as in __rt_task_send(): if the
		 * size of the reply message can fit into our local
		 * buffer, use it; otherwise, take the slow path and
		 * fetch a larger buffer from the system heap.
		 */
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

static int __rt_timer_read(RTIME __user *u_time)
{
	RTIME now = rt_timer_read();

	return __xn_safe_copy_to_user(u_time, &now, sizeof(now));
}

static int __rt_timer_tsc(RTIME __user *u_tsc)
{
	RTIME tsc = rt_timer_tsc();

	return __xn_safe_copy_to_user(u_tsc, &tsc, sizeof(tsc));
}

static int __rt_timer_ns2ticks(SRTIME __user *u_ticks,
			       SRTIME __user *u_ns)
{
	SRTIME ns, ticks;

	if (__xn_safe_copy_from_user(&ns, u_ns, sizeof(ns)))
		return -EFAULT;

	ticks = rt_timer_ns2ticks(ns);

	return __xn_safe_copy_to_user(u_ticks, &ticks, sizeof(ticks));
}

static int __rt_timer_ticks2ns(SRTIME __user *u_ns,
			       SRTIME __user *u_ticks)
{
	SRTIME ticks, ns;

	if (__xn_safe_copy_from_user(&ticks, u_ticks, sizeof(ticks)))
		return -EFAULT;

	ns = rt_timer_ticks2ns(ticks);

	return __xn_safe_copy_to_user(u_ns, &ns, sizeof(ns));
}

static int __rt_timer_inquire(RT_TIMER_INFO __user *u_info)
{
	RT_TIMER_INFO info;
	int ret;

	ret = rt_timer_inquire(&info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
}

static int __rt_timer_spin(RTIME *u_ns)
{
	xnthread_t *thread = xnpod_current_thread();
	struct task_struct *p = current;
	RTIME etime;
	RTIME ns;

	if (__xn_safe_copy_from_user(&ns, u_ns, sizeof(ns)))
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

static int __rt_sem_create(RT_SEM_PLACEHOLDER __user *u_ph,
			   const char __user *u_name,
			   unsigned icount,
			   int mode)
{
	char name[XNOBJECT_NAME_LEN];
	RT_SEM_PLACEHOLDER ph;
	RT_SEM *sem;
	int err;

	if (u_name) {
		if (__xn_safe_strncpy_from_user(name,
						u_name,
						sizeof(name) - 1) < 0)
			return -EFAULT;
		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	sem = xnmalloc(sizeof(*sem));
	if (sem == NULL)
		return -ENOMEM;

	err = rt_sem_create(sem, name, icount, mode);
	if (err == 0) {
		sem->cpid = current->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = sem->handle;
		if (__xn_safe_copy_to_user(u_ph, &ph, sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(sem);

	return err;
}

static int __rt_sem_bind(RT_SEM_PLACEHOLDER __user *u_ph,
			 const char __user *u_name,
			 RTIME __user *u_timeout)
{
	RT_SEM_PLACEHOLDER ph;
	int ret;

	ret = __rt_bind_helper(u_name, u_timeout,
			       &ph.opaque, XENO_SEM_MAGIC,
			       NULL, 0);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_ph, &ph, sizeof(ph));
}

static int __rt_sem_delete(RT_SEM_PLACEHOLDER __user *u_ph)
{
	RT_SEM_PLACEHOLDER ph;
	RT_SEM *sem;
	int err;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	sem = xnregistry_fetch(ph.opaque);
	if (sem == NULL)
		return -ESRCH;

	err = rt_sem_delete(sem);
	if (err == 0 && sem->cpid)
		xnfree(sem);

	return err;
}

static int __rt_sem_p(RT_SEM_PLACEHOLDER __user *u_ph,
		      xntmode_t timeout_mode,
		      RTIME __user *u_timeout)
{
	RT_SEM_PLACEHOLDER ph;
	RTIME timeout;
	RT_SEM *sem;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	sem = xnregistry_fetch(ph.opaque);
	if (sem == NULL)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	return rt_sem_p_inner(sem, timeout_mode, timeout);
}

static int __rt_sem_v(RT_SEM_PLACEHOLDER __user *u_ph)
{
	RT_SEM_PLACEHOLDER ph;
	RT_SEM *sem;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	sem = xnregistry_fetch(ph.opaque);
	if (sem == NULL)
		return -ESRCH;

	return rt_sem_v(sem);
}

static int __rt_sem_broadcast(RT_SEM_PLACEHOLDER __user *u_ph)
{
	RT_SEM_PLACEHOLDER ph;
	RT_SEM *sem;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	sem = xnregistry_fetch(ph.opaque);
	if (sem == NULL)
		return -ESRCH;

	return rt_sem_broadcast(sem);
}

static int __rt_sem_inquire(RT_SEM_PLACEHOLDER __user *u_ph,
			    RT_SEM_INFO __user *u_info)
{
	RT_SEM_PLACEHOLDER ph;
	RT_SEM_INFO info;
	RT_SEM *sem;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	sem = xnregistry_fetch(ph.opaque);
	if (sem == NULL)
		return -ESRCH;

	ret = rt_sem_inquire(sem, &info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
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

static int __rt_event_create(RT_EVENT_PLACEHOLDER __user *u_ph,
			     const char __user *u_name,
			     unsigned ivalue,
			     int mode)
{
	char name[XNOBJECT_NAME_LEN];
	RT_EVENT_PLACEHOLDER ph;
	RT_EVENT *event;
	int err;

	if (u_name) {
		if (__xn_safe_strncpy_from_user(name, u_name,
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	event = xnmalloc(sizeof(*event));
	if (event == NULL)
		return -ENOMEM;

	err = rt_event_create(event, name, ivalue, mode);
	if (err == 0) {
		event->cpid = current->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = event->handle;
		if (__xn_safe_copy_to_user(u_ph, &ph, sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(event);

	return err;
}

static int __rt_event_bind(RT_EVENT_PLACEHOLDER __user *u_ph,
			   const char __user *u_name,
			   RTIME __user *u_timeout)
{
	RT_EVENT_PLACEHOLDER ph;
	int ret;

	ret = __rt_bind_helper(u_name, u_timeout,
			       &ph.opaque, XENO_EVENT_MAGIC,
			       NULL, 0);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_ph, &ph, sizeof(ph));
}

static int __rt_event_delete(RT_EVENT_PLACEHOLDER __user *u_ph)
{
	RT_EVENT_PLACEHOLDER ph;
	RT_EVENT *event;
	int err;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	event = xnregistry_fetch(ph.opaque);
	if (event == NULL)
		return -ESRCH;

	err = rt_event_delete(event);
	if (err == 0 && event->cpid)
		xnfree(event);

	return err;
}

static int __rt_event_wait(RT_EVENT_PLACEHOLDER __user *u_ph,
			   unsigned long __user *u_mask,
			   int mode,
			   xntmode_t timeout_mode,
			   RTIME __user *u_timeout)
{
	unsigned long mask, mask_r;
	RT_EVENT_PLACEHOLDER ph;
	RT_EVENT *event;
	RTIME timeout;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&mask, u_mask, sizeof(mask)))
		return -EFAULT;

	event = xnregistry_fetch(ph.opaque);
	if (event == NULL)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	ret = rt_event_wait_inner(event, mask, &mask_r, mode, timeout_mode, timeout);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_mask, &mask_r, sizeof(mask_r));
}

static int __rt_event_signal(RT_EVENT_PLACEHOLDER __user *u_ph,
			     unsigned long mask)
{
	RT_EVENT_PLACEHOLDER ph;
	RT_EVENT *event;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	event = xnregistry_fetch(ph.opaque);
	if (event == NULL)
		return -ESRCH;

	return rt_event_signal(event, mask);
}

static int __rt_event_clear(RT_EVENT_PLACEHOLDER __user *u_ph,
			    unsigned long mask,
			    unsigned long __user *u_mask_r)
{
	RT_EVENT_PLACEHOLDER ph;
	unsigned long mask_r;
	RT_EVENT *event;
	int err;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	event = xnregistry_fetch(ph.opaque);
	if (event == NULL)
		return -ESRCH;

	err = rt_event_clear(event, mask, &mask_r);
	if (err == 0 && u_mask_r)
		if (__xn_safe_copy_to_user(u_mask_r,
					   &mask_r, sizeof(mask_r)))
			err = -EFAULT;
	return err;
}

static int __rt_event_inquire(RT_EVENT_PLACEHOLDER __user *u_ph,
			      RT_EVENT_INFO __user *u_info)
{
	RT_EVENT_PLACEHOLDER ph;
	RT_EVENT_INFO info;
	RT_EVENT *event;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	event = xnregistry_fetch(ph.opaque);
	if (event == NULL)
		return -ESRCH;

	ret = rt_event_inquire(event, &info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
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

static int __rt_mutex_create(RT_MUTEX_PLACEHOLDER __user *u_ph,
			     const char __user *u_name)
{
	char name[XNOBJECT_NAME_LEN];
	RT_MUTEX_PLACEHOLDER ph;
	xnheap_t *sem_heap;
	RT_MUTEX *mutex;
	int err;

	if (u_name) {
		if (__xn_safe_strncpy_from_user(name, u_name,
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
	if (__xn_safe_copy_to_user(u_ph, &ph, sizeof(ph))) {
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

static int __rt_mutex_bind(RT_MUTEX_PLACEHOLDER __user *u_ph,
			   const char __user *u_name,
			   RTIME __user *u_timeout)
{
	RT_MUTEX_PLACEHOLDER ph;
	RT_MUTEX *mutex;
	int ret;

	ret = __rt_bind_helper(u_name, u_timeout,
			       &ph.opaque, XENO_MUTEX_MAGIC,
			       (void **)&mutex, 0);
	if (ret)
		return ret;

#ifdef CONFIG_XENO_FASTSYNCH
	ph.fastlock = (void *)xnheap_mapped_offset(&xnsys_ppd_get(1)->sem_heap,
						   mutex->synch_base.fastlock);
#endif /* CONFIG_XENO_FASTSYNCH */

	return __xn_safe_copy_to_user(u_ph, &ph, sizeof(ph));
}

static int __rt_mutex_delete(RT_MUTEX_PLACEHOLDER __user *u_ph)
{
	RT_MUTEX_PLACEHOLDER ph;
	RT_MUTEX *mutex;
	int err;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	mutex = xnregistry_fetch(ph.opaque);
	if (mutex == NULL)
		return -ESRCH;

	err = rt_mutex_delete(mutex);
	if (!err && mutex->cpid)
		xnfree(mutex);

	return err;
}

static int __rt_mutex_acquire(RT_MUTEX_PLACEHOLDER __user *u_ph,
			      xntmode_t timeout_mode,
			      RTIME __user *u_timeout)
{
	xnhandle_t mutexh;
	RT_MUTEX *mutex;
	RTIME timeout;

	if (__xn_safe_copy_from_user(&mutexh, &u_ph->opaque, sizeof(mutexh)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	mutex = xnregistry_fetch(mutexh);
	if (mutex == NULL)
		return -ESRCH;

	return rt_mutex_acquire_inner(mutex, timeout, timeout_mode);
}

static int __rt_mutex_release(RT_MUTEX_PLACEHOLDER __user *u_ph)
{
	xnhandle_t mutexh;
	RT_MUTEX *mutex;

	if (__xn_safe_copy_from_user(&mutexh, &u_ph->opaque, sizeof(mutexh)))
		return -EFAULT;

	mutex = xnregistry_fetch(mutexh);
	if (mutex == NULL)
		return -ESRCH;

	return rt_mutex_release(mutex);
}

static int __rt_mutex_inquire(RT_MUTEX_PLACEHOLDER __user *u_ph,
			      RT_MUTEX_INFO __user *u_info)
{
	RT_MUTEX_PLACEHOLDER ph;
	RT_MUTEX_INFO info;
	RT_MUTEX *mutex;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	mutex = xnregistry_fetch(ph.opaque);
	if (mutex == NULL)
		return -ESRCH;

	ret = rt_mutex_inquire(mutex, &info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
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

static int __rt_cond_create(RT_COND_PLACEHOLDER __user *u_ph,
			    const char __user *u_name)
{
	char name[XNOBJECT_NAME_LEN];
	RT_COND_PLACEHOLDER ph;
	RT_COND *cond;
	int err;

	if (u_name) {
		if (__xn_safe_strncpy_from_user(name, u_name,
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	cond = xnmalloc(sizeof(*cond));
	if (cond == NULL)
		return -ENOMEM;

	err = rt_cond_create(cond, name);
	if (err == 0) {
		cond->cpid = current->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = cond->handle;
		if (__xn_safe_copy_to_user(u_ph, &ph, sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(cond);

	return err;
}

static int __rt_cond_bind(RT_COND_PLACEHOLDER __user *u_ph,
			  const char __user *u_name,
			  RTIME __user *u_timeout)
{
	RT_COND_PLACEHOLDER ph;
	int ret;

	ret = __rt_bind_helper(u_name, u_timeout,
			       &ph.opaque, XENO_COND_MAGIC, NULL, 0);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_ph, &ph, sizeof(ph));
}

static int __rt_cond_delete(RT_COND_PLACEHOLDER __user *u_ph)
{
	RT_COND_PLACEHOLDER ph;
	RT_COND *cond;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	cond = xnregistry_fetch(ph.opaque);
	if (cond == NULL)
		return -ESRCH;

	ret = rt_cond_delete(cond);
	if (ret == 0 && cond->cpid)
		xnfree(cond);

	return ret;
}

struct us_cond_data {
	unsigned lockcnt;
	int err;
};

static int __rt_cond_wait_prologue(RT_COND_PLACEHOLDER __user *u_cph,
				   RT_MUTEX_PLACEHOLDER __user *u_mph,
				   struct us_cond_data __user *u_cond,
				   xntmode_t timeout_mode,
				   RTIME  __user *u_timeout)
{
	RT_COND_PLACEHOLDER cph, mph;
	unsigned dummy, *plockcnt;
	struct us_cond_data d;
	int ret, pret = 0;
	RT_MUTEX *mutex;
	RT_COND *cond;
	RTIME timeout;

	if (__xn_safe_copy_from_user(&cph, u_cph, sizeof(cph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&mph, u_mph, sizeof(mph)))
		return -EFAULT;

	cond = xnregistry_fetch(cph.opaque);
	if (cond == NULL)
		return -ESRCH;

	mutex = xnregistry_fetch(mph.opaque);
	if (mutex == NULL)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

#ifdef CONFIG_XENO_FASTSYNCH
	if (__xn_safe_copy_from_user(&d, u_cond, sizeof(d)))
		return -EFAULT;

	plockcnt = &dummy;
#else /* !CONFIG_XENO_FASTSYNCH */
	plockcnt = &d.lockcnt;
	(void)dummy;
#endif /* !CONFIG_XENO_FASTSYNCH */

	ret = rt_cond_wait_prologue(cond, mutex, plockcnt, timeout_mode, timeout);

	switch(ret) {
	case 0:
	case -ETIMEDOUT:
	case -EIDRM:
		pret = d.err = ret;
		ret = rt_cond_wait_epilogue(mutex, *plockcnt);
		break;

	case -EINTR:
		pret = ret;
		d.err = 0; /* epilogue should return 0. */
		break;
	}

	if (__xn_safe_copy_to_user(u_cond, &d, sizeof(d)))
		return -EFAULT;

	return ret == 0 ? pret : ret;
}

static int __rt_cond_wait_epilogue(RT_MUTEX_PLACEHOLDER __user *u_mph,
				   unsigned int lockcnt)
{
	RT_COND_PLACEHOLDER mph;
	RT_MUTEX *mutex;

	if (__xn_safe_copy_from_user(&mph, u_mph, sizeof(mph)))
		return -EFAULT;

	mutex = xnregistry_fetch(mph.opaque);
	if (mutex == NULL)
		return -ESRCH;

	return rt_cond_wait_epilogue(mutex, lockcnt);
}

static int __rt_cond_signal(RT_COND_PLACEHOLDER __user *u_ph)
{
	RT_COND_PLACEHOLDER ph;
	RT_COND *cond;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	cond = xnregistry_fetch(ph.opaque);
	if (cond == NULL)
		return -ESRCH;

	return rt_cond_signal(cond);
}

static int __rt_cond_broadcast(RT_COND_PLACEHOLDER __user *u_ph)
{
	RT_COND_PLACEHOLDER ph;
	RT_COND *cond;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	cond = xnregistry_fetch(ph.opaque);
	if (cond == NULL)
		return -ESRCH;

	return rt_cond_broadcast(cond);
}

static int __rt_cond_inquire(RT_COND_PLACEHOLDER __user *u_ph,
			     RT_COND_INFO __user *u_info)
{
	RT_COND_PLACEHOLDER ph;
	RT_COND_INFO info;
	RT_COND *cond;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	cond = xnregistry_fetch(ph.opaque);
	if (cond == NULL)
		return -ESRCH;

	ret = rt_cond_inquire(cond, &info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
}

#else /* !CONFIG_XENO_OPT_NATIVE_COND */

#define __rt_cond_create	__rt_call_not_available
#define __rt_cond_bind		__rt_call_not_available
#define __rt_cond_delete	__rt_call_not_available
#define __rt_cond_wait_prologue __rt_call_not_available
#define __rt_cond_wait_epilogue __rt_call_not_available
#define __rt_cond_signal	__rt_call_not_available
#define __rt_cond_broadcast	__rt_call_not_available
#define __rt_cond_inquire	__rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_COND */

#ifdef CONFIG_XENO_OPT_NATIVE_QUEUE

static int __rt_queue_create(RT_QUEUE_PLACEHOLDER __user *u_ph,
			     const char __user *u_name,
			     size_t poolsize,
			     size_t qlimit,
			     int mode)
{
	char name[XNOBJECT_NAME_LEN];
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;
	int ret;

	if (u_name) {
		if (__xn_safe_strncpy_from_user(name, u_name,
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	q = xnmalloc(sizeof(*q));
	if (q == NULL)
		return -ENOMEM;

	ret = rt_queue_create(q, name, poolsize, qlimit, mode);
	if (ret)
		goto free_and_fail;

	q->cpid = current->pid;

	/* Copy back the registry handle to the ph struct. */
	ph.opaque = q->handle;
	ph.opaque2 = &q->bufpool;
	ph.mapsize = xnheap_extentsize(&q->bufpool);
	ph.area = xnheap_base_memory(&q->bufpool);

	return __xn_safe_copy_to_user(u_ph, &ph, sizeof(ph));

      free_and_fail:

	xnfree(q);

	return ret;
}

static int __rt_queue_bind(RT_QUEUE_PLACEHOLDER __user *u_ph,
			   const char __user *u_name,
			   RTIME __user *u_timeout)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;
	int ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	ret = __rt_bind_helper(u_name, u_timeout,
			       &ph.opaque, XENO_QUEUE_MAGIC,
			       (void **)&q, 0);

	if (ret)
		goto unlock_and_exit;

	ph.opaque2 = &q->bufpool;
	ph.mapsize = xnheap_extentsize(&q->bufpool);
	ph.area = xnheap_base_memory(&q->bufpool);
	xnlock_put_irqrestore(&nklock, s);

	if (__xn_safe_copy_to_user(u_ph, &ph, sizeof(ph)))
		return -EFAULT;

	/*
	 * We might need to migrate to secondary mode now for mapping
	 * the pool memory to user-space; since this syscall is
	 * conforming, we might have entered it in primary mode.
	 */
	if (xnpod_primary_p())
		xnshadow_relax(0, 0);

	return 0;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static int __rt_queue_delete(RT_QUEUE_PLACEHOLDER __user *u_ph)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	q = xnregistry_fetch(ph.opaque);
	if (q == NULL)
		return -ESRCH;

	/* Callee will check the queue descriptor for validity again. */
	return rt_queue_delete_inner(q, (void __user *)ph.mapbase);
}

static int __rt_queue_alloc(RT_QUEUE_PLACEHOLDER __user *u_ph,
			    size_t size,
			    void __user **u_bufp)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;
	int ret = 0;
	void *buf;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	q = xnregistry_fetch(ph.opaque);
	if (q == NULL) {
		ret = -ESRCH;
		buf = NULL;
		goto unlock_and_exit;
	}

	buf = rt_queue_alloc(q, size);

	/*
	 * Convert the kernel-based address of buf to the equivalent
	 * area into the caller's address space.
	 */
	if (buf)
		buf = ph.mapbase + xnheap_mapped_offset(&q->bufpool, buf);
	else
		ret = -ENOMEM;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	if (__xn_safe_copy_to_user(u_bufp, &buf, sizeof(buf)))
		return -EFAULT;

	return ret;
}

/*
 * int __rt_queue_free()
 */

static int __rt_queue_free(RT_QUEUE_PLACEHOLDER __user *u_ph,
			   void __user *u_buf)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;
	void *buf;
	int ret;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	q = xnregistry_fetch(ph.opaque);
	if (q == NULL) {
		ret = -ESRCH;
		goto unlock_and_exit;
	}

	/*
	 * Convert the caller-based address of buf to the equivalent area
	 * into the kernel address space. We don't know whether u_buf is
	 * valid memory yet, do not dereference it.
	 */
	if (u_buf) {
		buf =
		    xnheap_mapped_address(&q->bufpool,
					  (caddr_t)u_buf - ph.mapbase);
		ret = rt_queue_free(q, buf);
	} else
		ret = -EINVAL;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static int __rt_queue_send(RT_QUEUE_PLACEHOLDER __user *u_ph,
			   void __user *u_buf,
			   size_t size,
			   int mode)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;
	void *buf;
	int ret;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	q = xnregistry_fetch(ph.opaque);
	if (q == NULL) {
		ret = -ESRCH;
		goto unlock_and_exit;
	}

	/*
	 * Convert the caller-based address of buf to the equivalent
	 * area into the kernel address space. We don't know whether
	 * u_buf is valid memory yet, do not dereference it.
	 */
	if (u_buf) {
		buf =
		    xnheap_mapped_address(&q->bufpool,
					  (caddr_t)u_buf - ph.mapbase);
		ret = rt_queue_send(q, buf, size, mode);
	} else
		ret = -EINVAL;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static int __rt_queue_write(RT_QUEUE_PLACEHOLDER __user *u_ph,
			    const void __user *u_buf,
			    size_t size,
			    int mode)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;
	void *mbuf;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	q = xnregistry_fetch(ph.opaque);
	if (q == NULL)
		return -ESRCH;

	mbuf = rt_queue_alloc(q, size);
	if (mbuf == NULL)
		return -ENOMEM;

	if (size > 0) {
		/* Slurp the message directly into the conveying buffer. */
		if (__xn_safe_copy_from_user(mbuf, u_buf, size)) {
			rt_queue_free(q, mbuf);
			return -EFAULT;
		}
	}

	ret = rt_queue_send(q, mbuf, size, mode);
	if (ret == 0 && (mode & Q_BROADCAST))
		/* Nobody received, free the buffer. */
		rt_queue_free(q, mbuf);

	return ret;
}

static int __rt_queue_receive(RT_QUEUE_PLACEHOLDER __user *u_ph,
			      void __user **u_buf,
			      xntmode_t timeout_mode,
			      RTIME __user *u_timeout)
{
	RT_QUEUE_PLACEHOLDER ph;
	RTIME timeout;
	RT_QUEUE *q;
	void *buf;
	int ret;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	q = xnregistry_fetch(ph.opaque);
	if (q == NULL) {
		xnlock_put_irqrestore(&nklock, s);
		ret = -ESRCH;
		goto out;
	}

	ret = rt_queue_receive_inner(q, &buf, timeout_mode, timeout);
	if (ret < 0) {
		xnlock_put_irqrestore(&nklock, s);
		goto out;
	}

	/*
	 * Convert the kernel-based address of buf to the equivalent area
	 * into the caller's address space.
	 */
	buf = ph.mapbase + xnheap_mapped_offset(&q->bufpool, buf);

	xnlock_put_irqrestore(&nklock, s);

	if (__xn_safe_copy_to_user(u_buf, &buf, sizeof(buf)))
		ret = -EFAULT;
out:

	return ret;
}

static int __rt_queue_read(RT_QUEUE_PLACEHOLDER __user *u_ph,
			   void __user *u_buf,
			   size_t size,
			   xntmode_t timeout_mode,
			   RTIME __user *u_timeout)
{
	RT_QUEUE_PLACEHOLDER ph;
	ssize_t rsize;
	RTIME timeout;
	RT_QUEUE *q;
	void *mbuf;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	q = xnregistry_fetch(ph.opaque);
	if (q == NULL)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	rsize = rt_queue_receive_inner(q, &mbuf, timeout_mode, timeout);
	if (rsize >= 0) {
		size = size < rsize ? size : rsize;

		if (size > 0 &&	__xn_safe_copy_to_user(u_buf, mbuf, size))
			rsize = -EFAULT;

		rt_queue_free(q, mbuf);
	}

	return (int)rsize;
}

static int __rt_queue_inquire(RT_QUEUE_PLACEHOLDER __user *u_ph,
			      RT_QUEUE_INFO __user *u_info)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE_INFO info;
	RT_QUEUE *q;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	q = xnregistry_fetch(ph.opaque);
	if (q == NULL)
		return -ESRCH;

	ret = rt_queue_inquire(q, &info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
}

static int __rt_queue_flush(RT_QUEUE_PLACEHOLDER __user *u_ph)
{
	RT_QUEUE_PLACEHOLDER ph;
	RT_QUEUE *q;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
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

static int __rt_heap_create(RT_HEAP_PLACEHOLDER __user *u_ph,
			    const char __user *u_name,
			    size_t heapsize,
			    int mode)
{
	char name[XNOBJECT_NAME_LEN];
	RT_HEAP_PLACEHOLDER ph;
	RT_HEAP *heap;
	int err;

	if (u_name) {
		if (__xn_safe_strncpy_from_user(name, u_name,
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	heap = xnmalloc(sizeof(*heap));
	if (heap == NULL)
		return -ENOMEM;

	err = rt_heap_create(heap, name, heapsize, mode);
	if (err)
		goto free_and_fail;

	heap->cpid = current->pid;

	/* Copy back the registry handle to the ph struct. */
	ph.opaque = heap->handle;
	ph.opaque2 = &heap->heap_base;
	ph.mapsize = xnheap_extentsize(&heap->heap_base);
	ph.area = xnheap_base_memory(&heap->heap_base);

	return __xn_safe_copy_to_user(u_ph, &ph, sizeof(ph));

free_and_fail:

	xnfree(heap);

	return err;
}

static int __rt_heap_bind(RT_HEAP_PLACEHOLDER __user *u_ph,
			  const char __user *u_name,
			  RTIME __user *u_timeout)
{
	RT_HEAP_PLACEHOLDER ph;
	RT_HEAP *heap;
	int ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	ret = __rt_bind_helper(u_name, u_timeout,
			       &ph.opaque, XENO_HEAP_MAGIC,
			       (void **)&heap, 0);
	if (ret)
		goto unlock_and_exit;

	ph.opaque2 = &heap->heap_base;
	ph.mapsize = xnheap_extentsize(&heap->heap_base);
	ph.area = xnheap_base_memory(&heap->heap_base);

	xnlock_put_irqrestore(&nklock, s);

	if (__xn_safe_copy_to_user(u_ph, &ph, sizeof(ph)))
		return -EFAULT;

	/* We might need to migrate to secondary mode now for mapping the
	   heap memory to user-space; since this syscall is conforming, we
	   might have entered it in primary mode. */

	if (xnpod_primary_p())
		xnshadow_relax(0, 0);

	return 0;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static int __rt_heap_delete(RT_HEAP_PLACEHOLDER __user *u_ph)
{
	RT_HEAP_PLACEHOLDER ph;
	RT_HEAP *heap;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	heap = xnregistry_fetch(ph.opaque);
	if (heap == NULL)
		return -ESRCH;

	/* Callee will check the heap descriptor for validity again. */
	return rt_heap_delete_inner(heap, (void __user *)ph.mapbase);
}

static int __rt_heap_alloc(RT_HEAP_PLACEHOLDER __user *u_ph,
			   size_t size,
			   RTIME __user *u_timeout,
			   void __user **u_bufp)
{
	RT_HEAP_PLACEHOLDER ph;
	void *buf = NULL;
	RT_HEAP *heap;
	RTIME timeout;
	int ret;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	heap = xnregistry_fetch(ph.opaque);
	if (heap == NULL) {
		ret = -ESRCH;
		goto unlock_and_exit;
	}

	ret = rt_heap_alloc(heap, size, timeout, &buf);

	/*
	 * Convert the kernel-based address of buf to the equivalent area
	 * into the caller's address space.
	 */
	if (ret == 0)
		buf = ph.mapbase + xnheap_mapped_offset(&heap->heap_base, buf);

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret ?: __xn_safe_copy_to_user(u_bufp, &buf, sizeof(buf));
}

static int __rt_heap_free(RT_HEAP_PLACEHOLDER __user *u_ph,
			  void __user *u_buf)
{
	RT_HEAP_PLACEHOLDER ph;
	RT_HEAP *heap;
	void *buf;
	int ret;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	heap = xnregistry_fetch(ph.opaque);
	if (heap == NULL) {
		ret = -ESRCH;
		goto unlock_and_exit;
	}

	/*
	 * Convert the caller-based address of buf to the equivalent area
	 * into the kernel address space.
	 */
	if (u_buf) {
		buf =
		    xnheap_mapped_address(&heap->heap_base,
					  (caddr_t)u_buf - ph.mapbase);
		ret = rt_heap_free(heap, buf);
	} else
		ret = -EINVAL;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

static int __rt_heap_inquire(RT_HEAP_PLACEHOLDER __user *u_ph,
			     RT_HEAP_INFO __user *u_info)
{
	RT_HEAP_PLACEHOLDER ph;
	RT_HEAP_INFO info;
	RT_HEAP *heap;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	heap = xnregistry_fetch(ph.opaque);
	if (heap == NULL)
		return -ESRCH;

	ret = rt_heap_inquire(heap, &info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
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

static int __rt_alarm_create(RT_ALARM_PLACEHOLDER __user *u_ph,
			     const char __user *u_name)
{
	char name[XNOBJECT_NAME_LEN];
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM *alarm;
	int err;

	if (u_name) {
		if (__xn_safe_strncpy_from_user(name, u_name,
						sizeof(name) - 1) < 0)
			return -EFAULT;

		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	alarm = xnmalloc(sizeof(*alarm));
	if (alarm == NULL)
		return -ENOMEM;

	err = rt_alarm_create(alarm, name, &rt_alarm_handler, NULL);
	if (likely(err == 0)) {
		alarm->cpid = current->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = alarm->handle;
		if (__xn_safe_copy_to_user(u_ph, &ph, sizeof(ph)))
			err = -EFAULT;
	} else
		xnfree(alarm);

	return err;
}

static int __rt_alarm_delete(RT_ALARM_PLACEHOLDER __user *u_ph)
{
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM *alarm;
	int err;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	alarm = xnregistry_fetch(ph.opaque);
	if (alarm == NULL)
		return -ESRCH;

	err = rt_alarm_delete(alarm);
	if (!err && alarm->cpid)
		xnfree(alarm);

	return err;
}

static int __rt_alarm_start(RT_ALARM_PLACEHOLDER __user *u_ph,
			    RTIME __user *u_value,
			    RTIME __user *u_interval)
{
	RT_ALARM_PLACEHOLDER ph;
	RTIME value, interval;
	RT_ALARM *alarm;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	alarm = xnregistry_fetch(ph.opaque);
	if (alarm == NULL)
		return -ESRCH;

	if (__xn_safe_copy_from_user(&value, u_value, sizeof(value)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&interval, u_interval, sizeof(interval)))
		return -EFAULT;

	return rt_alarm_start(alarm, value, interval);
}

static int __rt_alarm_stop(RT_ALARM_PLACEHOLDER __user *u_ph)
{
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM *alarm;

	if (__xn_safe_copy_from_user(&ph, u_ph,	sizeof(ph)))
		return -EFAULT;

	alarm = xnregistry_fetch(ph.opaque);
	if (alarm == NULL)
		return -ESRCH;

	return rt_alarm_stop(alarm);
}

static int __rt_alarm_wait(RT_ALARM_PLACEHOLDER __user *u_ph)
{
	xnthread_t *thread = xnpod_current_thread();
	union xnsched_policy_param param;
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM *alarm;
	xnflags_t info;
	int err = 0;
	spl_t s;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	xnlock_get_irqsave(&nklock, s);

	alarm = xeno_h2obj_validate(xnregistry_fetch(ph.opaque),
				    XENO_ALARM_MAGIC, RT_ALARM);
	if (alarm == NULL) {
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

static int __rt_alarm_inquire(RT_ALARM_PLACEHOLDER __user *u_ph,
			      RT_ALARM_INFO __user *u_info)
{
	RT_ALARM_PLACEHOLDER ph;
	RT_ALARM_INFO info;
	RT_ALARM *alarm;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	alarm = xnregistry_fetch(ph.opaque);
	if (alarm == NULL)
		return -ESRCH;

	ret = rt_alarm_inquire(alarm, &info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
}

#else /* !CONFIG_XENO_OPT_NATIVE_ALARM */

#define __rt_alarm_create     __rt_call_not_available
#define __rt_alarm_delete     __rt_call_not_available
#define __rt_alarm_start      __rt_call_not_available
#define __rt_alarm_stop       __rt_call_not_available
#define __rt_alarm_wait       __rt_call_not_available
#define __rt_alarm_inquire    __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_ALARM */

#ifdef CONFIG_XENO_OPT_NATIVE_BUFFER

static int __rt_buffer_create(RT_BUFFER_PLACEHOLDER __user *u_ph,
			      const char __user *u_name,
			      size_t bufsz,
			      int mode)
{
	char name[XNOBJECT_NAME_LEN];
	RT_BUFFER_PLACEHOLDER ph;
	RT_BUFFER *bf;
	int ret;

	if (u_name) {
		if (__xn_safe_strncpy_from_user(name, u_name,
						sizeof(name) - 1) < 0)
			return -EFAULT;
		name[sizeof(name) - 1] = '\0';
	} else
		*name = '\0';

	bf = xnmalloc(sizeof(*bf));
	if (bf == NULL)
		return -ENOMEM;

	ret = rt_buffer_create(bf, name, bufsz, mode);
	if (ret == 0) {
		bf->cpid = current->pid;
		/* Copy back the registry handle to the ph struct. */
		ph.opaque = bf->handle;
		if (__xn_safe_copy_to_user(u_ph, &ph, sizeof(ph)))
			ret = -EFAULT;
	} else
		xnfree(bf);

	return ret;
}

static int __rt_buffer_bind(RT_BUFFER_PLACEHOLDER __user *u_ph,
			    const char __user *u_name,
			    RTIME __user *u_timeout)
{
	RT_BUFFER_PLACEHOLDER ph;
	int ret;

	ret = __rt_bind_helper(u_name, u_timeout,
			       &ph.opaque, XENO_BUFFER_MAGIC,
			       NULL, 0);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_ph, &ph, sizeof(ph));
}

static int __rt_buffer_delete(RT_BUFFER_PLACEHOLDER __user *u_ph)
{
	RT_BUFFER_PLACEHOLDER ph;
	RT_BUFFER *bf;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	bf = xnregistry_fetch(ph.opaque);
	if (bf == NULL)
		return -ESRCH;

	ret = rt_buffer_delete(bf);
	if (ret == 0 && bf->cpid)
		xnfree(bf);

	return ret;
}

static int __rt_buffer_write(RT_BUFFER_PLACEHOLDER __user *u_ph,
			     const void __user *u_buf,
			     size_t size,
			     xntmode_t timeout_mode,
			     RTIME __user *u_timeout)
{
	RT_BUFFER_PLACEHOLDER ph;
	struct xnbufd bufd;
	RTIME timeout;
	RT_BUFFER *bf;
	ssize_t ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	bf = xnregistry_fetch(ph.opaque);
	if (bf == NULL)
		return -ESRCH;

	xnbufd_map_uread(&bufd, u_buf, size);
	ret = rt_buffer_write_inner(bf, &bufd, timeout_mode, timeout);
	xnbufd_unmap_uread(&bufd);

	return ret;
}

static int __rt_buffer_read(RT_BUFFER_PLACEHOLDER __user *u_ph,
			    void __user *u_buf,
			    size_t size,
			    xntmode_t timeout_mode,
			    RTIME __user *u_timeout)
{
	RT_BUFFER_PLACEHOLDER ph;
	struct xnbufd bufd;
	RTIME timeout;
	RT_BUFFER *bf;
	ssize_t ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	if (__xn_safe_copy_from_user(&timeout, u_timeout, sizeof(timeout)))
		return -EFAULT;

	bf = xnregistry_fetch(ph.opaque);
	if (bf == NULL)
		return -ESRCH;

	xnbufd_map_uwrite(&bufd, u_buf, size);
	ret = rt_buffer_read_inner(bf, &bufd, timeout_mode, timeout);
	xnbufd_unmap_uwrite(&bufd);

	return ret;
}

static int __rt_buffer_clear(RT_BUFFER_PLACEHOLDER __user *u_ph)
{
	RT_BUFFER_PLACEHOLDER ph;
	RT_BUFFER *bf;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	bf = xnregistry_fetch(ph.opaque);
	if (bf == NULL)
		return -ESRCH;

	return rt_buffer_clear(bf);
}

static int __rt_buffer_inquire(RT_BUFFER_PLACEHOLDER __user *u_ph,
			       RT_BUFFER_INFO __user *u_info)
{
	RT_BUFFER_PLACEHOLDER ph;
	RT_BUFFER_INFO info;
	RT_BUFFER *bf;
	int ret;

	if (__xn_safe_copy_from_user(&ph, u_ph, sizeof(ph)))
		return -EFAULT;

	bf = xnregistry_fetch(ph.opaque);
	if (bf == NULL)
		return -ESRCH;

	ret = rt_buffer_inquire(bf, &info);
	if (ret)
		return ret;

	return __xn_safe_copy_to_user(u_info, &info, sizeof(info));
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

static __attribute__ ((unused))
int __rt_call_not_available(void)
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
		initq(&rh->mutexq);
		initq(&rh->queueq);
		initq(&rh->semq);
		initq(&rh->bufferq);

		return &rh->ppd;

	case XNSHADOW_CLIENT_DETACH:

		rh = ppd2rholder((xnshadow_ppd_t *) data);
		__native_alarm_flush_rq(&rh->alarmq);
		__native_cond_flush_rq(&rh->condq);
		__native_event_flush_rq(&rh->eventq);
		__native_heap_flush_rq(&rh->heapq);
		__native_mutex_flush_rq(&rh->mutexq);
		__native_queue_flush_rq(&rh->queueq);
		__native_sem_flush_rq(&rh->semq);
		__native_buffer_flush_rq(&rh->bufferq);

		xnarch_free_host_mem(rh, sizeof(*rh));

		return NULL;
	}

	return ERR_PTR(-EINVAL);
}

static struct xnsysent __systab[] = {
	SKINCALL_DEF(__native_task_create, __rt_task_create, init),
	SKINCALL_DEF(__native_task_bind, __rt_task_bind, conforming),
	SKINCALL_DEF(__native_task_start, __rt_task_start, any),
	SKINCALL_DEF(__native_task_suspend, __rt_task_suspend, conforming),
	SKINCALL_DEF(__native_task_resume, __rt_task_resume, any),
	SKINCALL_DEF(__native_task_delete, __rt_task_delete, conforming),
	SKINCALL_DEF(__native_task_yield, __rt_task_yield, primary),
	SKINCALL_DEF(__native_task_set_periodic, __rt_task_set_periodic, conforming),
	SKINCALL_DEF(__native_task_wait_period, __rt_task_wait_period, primary),
	SKINCALL_DEF(__native_task_set_priority, __rt_task_set_priority, any),
	SKINCALL_DEF(__native_task_sleep, __rt_task_sleep, primary),
	SKINCALL_DEF(__native_task_sleep_until, __rt_task_sleep_until, primary),
	SKINCALL_DEF(__native_task_unblock, __rt_task_unblock, any),
	SKINCALL_DEF(__native_task_inquire, __rt_task_inquire, any),
	SKINCALL_DEF(__native_task_notify, __rt_task_notify, any),
	SKINCALL_DEF(__native_task_set_mode, __rt_task_set_mode, primary),
	SKINCALL_DEF(__native_task_self, __rt_task_self, any),
	SKINCALL_DEF(__native_task_slice, __rt_task_slice, any),
	SKINCALL_DEF(__native_task_send, __rt_task_send, primary),
	SKINCALL_DEF(__native_task_receive, __rt_task_receive, primary),
	SKINCALL_DEF(__native_task_reply, __rt_task_reply, primary),
	SKINCALL_DEF(__native_unimp_22, __rt_call_not_available, any),
	SKINCALL_DEF(__native_timer_read, __rt_timer_read, any),
	SKINCALL_DEF(__native_timer_tsc, __rt_timer_tsc, any),
	SKINCALL_DEF(__native_timer_ns2ticks, __rt_timer_ns2ticks, any),
	SKINCALL_DEF(__native_timer_ticks2ns, __rt_timer_ticks2ns, any),
	SKINCALL_DEF(__native_timer_inquire, __rt_timer_inquire, any),
	SKINCALL_DEF(__native_timer_spin, __rt_timer_spin, any),
	SKINCALL_DEF(__native_sem_create, __rt_sem_create, any),
	SKINCALL_DEF(__native_sem_bind, __rt_sem_bind, conforming),
	SKINCALL_DEF(__native_sem_delete, __rt_sem_delete, any),
	SKINCALL_DEF(__native_sem_p, __rt_sem_p, primary),
	SKINCALL_DEF(__native_sem_v, __rt_sem_v, any),
	SKINCALL_DEF(__native_sem_broadcast, __rt_sem_broadcast, any),
	SKINCALL_DEF(__native_sem_inquire, __rt_sem_inquire, any),
	SKINCALL_DEF(__native_event_create, __rt_event_create, any),
	SKINCALL_DEF(__native_event_bind, __rt_event_bind, conforming),
	SKINCALL_DEF(__native_event_delete, __rt_event_delete, any),
	SKINCALL_DEF(__native_event_wait, __rt_event_wait, primary),
	SKINCALL_DEF(__native_event_signal, __rt_event_signal, any),
	SKINCALL_DEF(__native_event_clear, __rt_event_clear, any),
	SKINCALL_DEF(__native_event_inquire, __rt_event_inquire, any),
	SKINCALL_DEF(__native_mutex_create, __rt_mutex_create, any),
	SKINCALL_DEF(__native_mutex_bind, __rt_mutex_bind, conforming),
	SKINCALL_DEF(__native_mutex_delete, __rt_mutex_delete, any),
	SKINCALL_DEF(__native_mutex_acquire, __rt_mutex_acquire, primary),
	SKINCALL_DEF(__native_mutex_release, __rt_mutex_release, primary),
	SKINCALL_DEF(__native_mutex_inquire, __rt_mutex_inquire, any),
	SKINCALL_DEF(__native_cond_create, __rt_cond_create, any),
	SKINCALL_DEF(__native_cond_bind, __rt_cond_bind, conforming),
	SKINCALL_DEF(__native_cond_delete, __rt_cond_delete, any),
	SKINCALL_DEF(__native_cond_wait_prologue, __rt_cond_wait_prologue, nonrestartable),
	SKINCALL_DEF(__native_cond_wait_epilogue, __rt_cond_wait_epilogue, primary),
	SKINCALL_DEF(__native_cond_signal, __rt_cond_signal, any),
	SKINCALL_DEF(__native_cond_broadcast, __rt_cond_broadcast, any),
	SKINCALL_DEF(__native_cond_inquire, __rt_cond_inquire, any),
	SKINCALL_DEF(__native_queue_create, __rt_queue_create, lostage),
	SKINCALL_DEF(__native_queue_bind, __rt_queue_bind, conforming),
	SKINCALL_DEF(__native_queue_delete, __rt_queue_delete, lostage),
	SKINCALL_DEF(__native_queue_alloc, __rt_queue_alloc, any),
	SKINCALL_DEF(__native_queue_free, __rt_queue_free, any),
	SKINCALL_DEF(__native_queue_send, __rt_queue_send, any),
	SKINCALL_DEF(__native_queue_write, __rt_queue_write, any),
	SKINCALL_DEF(__native_queue_receive, __rt_queue_receive, primary),
	SKINCALL_DEF(__native_queue_read, __rt_queue_read, primary),
	SKINCALL_DEF(__native_queue_inquire, __rt_queue_inquire, any),
	SKINCALL_DEF(__native_queue_flush, __rt_queue_flush, any),
	SKINCALL_DEF(__native_heap_create, __rt_heap_create, lostage),
	SKINCALL_DEF(__native_heap_bind, __rt_heap_bind, conforming),
	SKINCALL_DEF(__native_heap_delete, __rt_heap_delete, lostage),
	SKINCALL_DEF(__native_heap_alloc, __rt_heap_alloc, conforming),
	SKINCALL_DEF(__native_heap_free, __rt_heap_free, any),
	SKINCALL_DEF(__native_heap_inquire, __rt_heap_inquire, any),
	SKINCALL_DEF(__native_alarm_create, __rt_alarm_create, any),
	SKINCALL_DEF(__native_alarm_delete, __rt_alarm_delete, any),
	SKINCALL_DEF(__native_alarm_start, __rt_alarm_start, any),
	SKINCALL_DEF(__native_alarm_stop, __rt_alarm_stop, any),
	SKINCALL_DEF(__native_alarm_wait, __rt_alarm_wait, primary),
	SKINCALL_DEF(__native_alarm_inquire, __rt_alarm_inquire, any),
	SKINCALL_DEF(__native_unimp_89, __rt_call_not_available, any),
	SKINCALL_DEF(__native_unimp_92, __rt_call_not_available, any),
	SKINCALL_DEF(__native_unimp_93, __rt_call_not_available, any),
	SKINCALL_DEF(__native_buffer_create, __rt_buffer_create, lostage),
	SKINCALL_DEF(__native_buffer_bind, __rt_buffer_bind, conforming),
	SKINCALL_DEF(__native_buffer_delete, __rt_buffer_delete, lostage),
	SKINCALL_DEF(__native_buffer_read, __rt_buffer_read, conforming),
	SKINCALL_DEF(__native_buffer_write, __rt_buffer_write, conforming),
	SKINCALL_DEF(__native_buffer_clear, __rt_buffer_clear, any),
	SKINCALL_DEF(__native_buffer_inquire, __rt_buffer_inquire, any),
};

static struct xnskin_props __props = {
	.name = "native",
	.magic = XENO_SKIN_MAGIC,
	.nrcalls = sizeof(__systab) / sizeof(__systab[0]),
	.systab = __systab,
	.eventcb = &__shadow_eventcb,
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
