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
#include <nucleus/shadow.h>
#include <nucleus/registry.h>
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

/* This file implements the Xenomai syscall wrappers;
 *
 * o Unchecked uaccesses are used to fetch args since the syslib is
 * trusted. We currently assume that the caller's memory is locked and
 * committed.
 *
 * o All skin services (re-)check the object descriptor they are
 * passed; so there is no race between a call to xnregistry_fetch()
 * where the user-space handle is converted to a descriptor pointer,
 * and the use of it in the actual syscall.
 */

static int __muxid;

static int __rt_bind_helper (struct task_struct *curr,
			     struct pt_regs *regs,
			     xnhandle_t *handlep,
			     unsigned magic,
			     void **objaddrp)
{
    char name[XNOBJECT_NAME_LEN];
    RTIME timeout;
    void *objaddr;
    spl_t s;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	return -EFAULT;

    __xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg3(regs),sizeof(timeout));

    err = xnregistry_bind(name,timeout,handlep);

    if (!err)
	{
	xnlock_get_irqsave(&nklock,s);

	objaddr = xnregistry_fetch(*handlep);
	
	/* Also validate the type of the bound object. */

	if (xeno_test_magic(objaddr,magic))
	    {
	    if (objaddrp)
		*objaddrp = objaddr;
	    }
	else
	    err = -EACCES;

	xnlock_put_irqrestore(&nklock,s);
	}

    return err;
}

static RT_TASK *__rt_task_current (struct task_struct *curr)

{
    xnthread_t *thread = xnshadow_thread(curr);

    /* Don't call rt_task_self() which does not know about relaxed
       tasks, but rather use the shadow information directly. */

    if (!thread || xnthread_get_magic(thread) != XENO_SKIN_MAGIC)
	return NULL;

    return thread2rtask(thread); /* Convert TCB pointers. */
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
 * }
 */

static int __rt_task_create (struct task_struct *curr, struct pt_regs *regs)

{
    xncompletion_t __user *u_completion;
    char name[XNOBJECT_NAME_LEN];
    struct rt_arg_bulk bulk;
    RT_TASK_PLACEHOLDER ph;
    int err, prio, mode;
    RT_TASK *task;

    if (xnshadow_thread(curr))
	return -EBUSY;

    __xn_copy_from_user(curr,&bulk,(void __user *)__xn_reg_arg1(regs),sizeof(bulk));

    if (!__xn_access_ok(curr,VERIFY_WRITE,bulk.a1,sizeof(ph)))
	return -EFAULT;

    if (bulk.a2)
	{
	if (!__xn_access_ok(curr,VERIFY_READ,bulk.a2,sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)bulk.a2,sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	strncpy(curr->comm,name,sizeof(curr->comm));
	curr->comm[sizeof(curr->comm) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Task priority. */
    prio = bulk.a3;
    /* Task init mode & CPU affinity. */
    mode = bulk.a4 & (T_CPUMASK|T_SUSP);
    /* Completion descriptor our parent thread is pending on -- may be NULL. */
    u_completion = (xncompletion_t __user *)__xn_reg_arg2(regs);

    task = (RT_TASK *)xnmalloc(sizeof(*task));

    if (!task)
	return -ENOMEM;

    /* Force FPU support in user-space. This will lead to a no-op if
       the platform does not support it. */

    err = rt_task_create(task,name,0,prio,XNFPU|XNSHADOW|mode);

    if (err == 0)
	{
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = xnthread_handle(&task->thread_base);
	ph.opaque2 = bulk.a5;	/* hidden pthread_t identifier. */
	__xn_copy_to_user(curr,(void __user *)bulk.a1,&ph,sizeof(ph));
	err = xnshadow_map(&task->thread_base,u_completion);
	}
    else
	{
	xnfree(task);
	/* Unblock and pass back error code. */

	if (u_completion)
	    xnshadow_signal_completion(u_completion,err);
	}

    return err;
}

/*
 * int __rt_task_bind(RT_TASK_PLACEHOLDER *ph,
 *                    const char *name,
 *                    RTIME *timeoutp)
 */

static int __rt_task_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    err = __rt_bind_helper(curr,regs,&ph.opaque,XENO_TASK_MAGIC,NULL);

    if (!err)
	{
	/* We just don't know the associated user-space pthread
	   identifier -- clear it to prevent misuse. */
	ph.opaque2 = 0;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}

    return err;
}

/*
 * int __rt_task_start(RT_TASK_PLACEHOLDER *ph,
 *                     void (*entry)(void *cookie),
 *                     void *cookie)
 */

static int __rt_task_start (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    task = (RT_TASK *)xnregistry_fetch(ph.opaque);

    if (!task)
	return -ESRCH;

    return rt_task_start(task,
			 (void (*)(void *))__xn_reg_arg2(regs),
			 (void *)__xn_reg_arg3(regs));
}

/*
 * int __rt_task_suspend(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_suspend (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)xnregistry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    return rt_task_suspend(task);
}

/*
 * int __rt_task_resume(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_resume (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    task = (RT_TASK *)xnregistry_fetch(ph.opaque);

    if (!task)
	return -ESRCH;

    return rt_task_resume(task);
}

/*
 * int __rt_task_delete(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)xnregistry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    return rt_task_delete(task); /* TCB freed in delete hook. */
}

/*
 * int __rt_task_yield(void)
 */

static int __rt_task_yield (struct task_struct *curr, struct pt_regs *regs)
{
    return rt_task_yield();
}

/*
 * int __rt_task_set_periodic(RT_TASK_PLACEHOLDER *ph,
 *			         RTIME idate,
 *			         RTIME period)
 */

static int __rt_task_set_periodic (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RTIME idate, period;
    RT_TASK *task;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)xnregistry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    __xn_copy_from_user(curr,&idate,(void __user *)__xn_reg_arg2(regs),sizeof(idate));
    __xn_copy_from_user(curr,&period,(void __user *)__xn_reg_arg3(regs),sizeof(period));

    return rt_task_set_periodic(task,idate,period);
}

/*
 * int __rt_task_wait_period(unsigned long *overruns_r)
 */

static int __rt_task_wait_period (struct task_struct *curr, struct pt_regs *regs)
{
    unsigned long overruns;
    int err;

    if (__xn_reg_arg1(regs) &&
	!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(overruns)))
	return -EFAULT;

    err = rt_task_wait_period(&overruns);

    if (__xn_reg_arg1(regs) && (err == 0 || err == -ETIMEDOUT))
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&overruns,sizeof(overruns));

    return err;
}

/*
 * int __rt_task_set_priority(RT_TASK_PLACEHOLDER *ph,
 *                            int prio)
 */

static int __rt_task_set_priority (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;
    int prio;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)xnregistry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    prio = __xn_reg_arg2(regs);

    return rt_task_set_priority(task,prio);
}

/*
 * int __rt_task_sleep(RTIME delay)
 */

static int __rt_task_sleep (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME delay;

    __xn_copy_from_user(curr,&delay,(void __user *)__xn_reg_arg1(regs),sizeof(delay));

    return rt_task_sleep(delay);
}

/*
 * int __rt_task_sleep(RTIME delay)
 */

static int __rt_task_sleep_until (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME date;

    __xn_copy_from_user(curr,&date,(void __user *)__xn_reg_arg1(regs),sizeof(date));

    return rt_task_sleep_until(date);
}

/*
 * int __rt_task_unblock(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_unblock (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    task = (RT_TASK *)xnregistry_fetch(ph.opaque);

    if (!task)
	return -ESRCH;

    return rt_task_unblock(task);
}

/*
 * int __rt_task_inquire(RT_TASK_PLACEHOLDER *ph,
 *                       RT_TASK_INFO *infop)
 */

static int __rt_task_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK_INFO info;
    RT_TASK *task;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)xnregistry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    err = rt_task_inquire(task,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

/*
 * int __rt_task_notify(RT_TASK_PLACEHOLDER *ph,
 *                      rt_sigset_t signals)
 */

static int __rt_task_notify (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    rt_sigset_t signals;
    RT_TASK *task;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)xnregistry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    signals = (rt_sigset_t)__xn_reg_arg2(regs);

    return rt_task_notify(task,signals);
}

/*
 * int __rt_task_set_mode(int clrmask,
 *                        int setmask,
 *                        int *mode_r)
 */

static int __rt_task_set_mode (struct task_struct *curr, struct pt_regs *regs)

{
    int err, setmask, clrmask, mode_r;

    if (__xn_reg_arg3(regs) &&
	!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(int)))
	return -EFAULT;

    clrmask = __xn_reg_arg1(regs);
    setmask = __xn_reg_arg2(regs);

    err = rt_task_set_mode(clrmask & ~T_PRIMARY,setmask & ~T_PRIMARY,&mode_r);

    if (err)
	return err;

    if (__xn_reg_arg3(regs))
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg3(regs),&mode_r,sizeof(mode_r));

    if ((clrmask & T_PRIMARY) != 0)
	xnshadow_relax(0);

    return 0;
}

/*
 * int __rt_task_self(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_self (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    task = __rt_task_current(curr);

    if (!task)
	/* Calls on behalf of a non-task context beget an error for
	   the user-space interface. */
	return -ESRCH;

    ph.opaque = xnthread_handle(&task->thread_base); /* Copy back the task handle. */

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return 0;
}

/*
 * int __rt_task_slice(RT_TASK_PLACEHOLDER *ph,
 *                     RTIME quantum)
 */

static int __rt_task_slice (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;
    RTIME quantum;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)xnregistry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    __xn_copy_from_user(curr,&quantum,(void __user *)__xn_reg_arg2(regs),sizeof(quantum));

    return rt_task_slice(task,quantum);
}

#ifdef CONFIG_XENO_OPT_NATIVE_MPS

/*
 * int __rt_task_send(RT_TASK_PLACEHOLDER *ph,
 *                    RT_TASK_MCB *mcb_s,
 *                    RT_TASK_MCB *mcb_r,
 *                    RTIME timeout)
 */

static int __rt_task_send (struct task_struct *curr, struct pt_regs *regs)

{
    char tmp_buf[RT_MCB_FSTORE_LIMIT];
    RT_TASK_MCB mcb_s, mcb_r;
    caddr_t tmp_area, data_r;
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;
    RTIME timeout;
    size_t xsize;
    ssize_t err;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)xnregistry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(mcb_s)))
	return -EFAULT;

    __xn_copy_from_user(curr,&mcb_s,(void __user *)__xn_reg_arg2(regs),sizeof(mcb_s));

    if (mcb_s.size > 0 && !__xn_access_ok(curr,VERIFY_READ,mcb_s.data,mcb_s.size))
	return -EFAULT;

    if (__xn_reg_arg3(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(mcb_r)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&mcb_r,(void __user *)__xn_reg_arg3(regs),sizeof(mcb_r));

	if (mcb_r.size > 0 && !__xn_access_ok(curr,VERIFY_WRITE,mcb_r.data,mcb_r.size))
	    return -EFAULT;
	}
    else
	{
	mcb_r.data = NULL;
	mcb_r.size = 0;
	}

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg4(regs),sizeof(timeout)))
	return -EFAULT;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg4(regs),sizeof(timeout));

    xsize = mcb_s.size + mcb_r.size;
    data_r = mcb_r.data;

    if (xsize > 0)
	{
	/* Try optimizing a bit here: if the cumulated message sizes
	   (initial+reply) can fit into our local buffer, use it;
	   otherwise, take the slow path and fetch a larger buffer
	   from the system heap. Most messages are expected to be
	   short enough to fit on the stack anyway. */

	if (xsize <= sizeof(tmp_buf))
	    tmp_area = tmp_buf;
	else
	    {
	    tmp_area = xnmalloc(xsize);

	    if (!tmp_area)
		return -ENOMEM;
	    }

	if (mcb_s.size > 0)
	    __xn_copy_from_user(curr,tmp_area,(void __user *)mcb_s.data,mcb_s.size);

	mcb_s.data = tmp_area;
	mcb_r.data = tmp_area + mcb_s.size;
	}
    else
	tmp_area = NULL;

    err = rt_task_send(task,&mcb_s,&mcb_r,timeout);

    if (err > 0)
	__xn_copy_to_user(curr,(void __user *)data_r,mcb_r.data,mcb_r.size);

    if (__xn_reg_arg3(regs))
	{
	mcb_r.data = data_r;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg3(regs),&mcb_r,sizeof(mcb_r));
	}

    if (tmp_area && tmp_area != tmp_buf)
	xnfree(tmp_area);

    return err;
}

/*
 * int __rt_task_receive(RT_TASK_MCB *mcb_r,
 *                       RTIME timeout)
 */

static int __rt_task_receive (struct task_struct *curr, struct pt_regs *regs)

{
    char tmp_buf[RT_MCB_FSTORE_LIMIT];
    caddr_t tmp_area, data_r;
    RT_TASK_MCB mcb_r;
    RTIME timeout;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(mcb_r)))
	return -EFAULT;

    __xn_copy_from_user(curr,&mcb_r,(void __user *)__xn_reg_arg1(regs),sizeof(mcb_r));

    if (mcb_r.size > 0 && !__xn_access_ok(curr,VERIFY_WRITE,mcb_r.data,mcb_r.size))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(timeout)))
	return -EFAULT;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg2(regs),sizeof(timeout));

    data_r = mcb_r.data;

    if (mcb_r.size > 0)
	{
	/* Same optimization as in __rt_task_send(): if the size of
	   the reply message can fit into our local buffer, use it;
	   otherwise, take the slow path and fetch a larger buffer
	   from the system heap. */

	if (mcb_r.size <= sizeof(tmp_buf))
	    tmp_area = tmp_buf;
	else
	    {
	    tmp_area = xnmalloc(mcb_r.size);

	    if (!tmp_area)
		return -ENOMEM;
	    }

	mcb_r.data = tmp_area;
	}
    else
	tmp_area = NULL;

    err = rt_task_receive(&mcb_r,timeout);

    if (err > 0 && mcb_r.size > 0)
	__xn_copy_to_user(curr,(void __user *)data_r,mcb_r.data,mcb_r.size);

    mcb_r.data = data_r;
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&mcb_r,sizeof(mcb_r));

    if (tmp_area && tmp_area != tmp_buf)
	xnfree(tmp_area);

    return err;
}

/*
 * int __rt_task_reply(int flowid,
 *                     RT_TASK_MCB *mcb_s)
 */

static int __rt_task_reply (struct task_struct *curr, struct pt_regs *regs)

{
    char tmp_buf[RT_MCB_FSTORE_LIMIT];
    RT_TASK_MCB mcb_s;
    caddr_t tmp_area;
    int flowid, err;

    flowid = __xn_reg_arg1(regs);

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(mcb_s)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&mcb_s,(void __user *)__xn_reg_arg2(regs),sizeof(mcb_s));

	if (mcb_s.size > 0 && !__xn_access_ok(curr,VERIFY_READ,mcb_s.data,mcb_s.size))
	    return -EFAULT;
	}
    else
	{
	mcb_s.data = NULL;
	mcb_s.size = 0;
	}

    if (mcb_s.size > 0)
	{
	/* Same optimization as in __rt_task_send(): if the size of
	   the reply message can fit into our local buffer, use it;
	   otherwise, take the slow path and fetch a larger buffer
	   from the system heap. */

	if (mcb_s.size <= sizeof(tmp_buf))
	    tmp_area = tmp_buf;
	else
	    {
	    tmp_area = xnmalloc(mcb_s.size);

	    if (!tmp_area)
		return -ENOMEM;
	    }

	__xn_copy_from_user(curr,tmp_area,(void __user *)mcb_s.data,mcb_s.size);
	mcb_s.data = tmp_area;
	}
    else
	tmp_area = NULL;

    err = rt_task_reply(flowid,&mcb_s);

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

static int __rt_timer_set_mode (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME tickval;
    __xn_copy_from_user(curr,&tickval,(void __user *)__xn_reg_arg1(regs),sizeof(tickval));
    return rt_timer_set_mode(tickval);
}

/*
 * int __rt_timer_read(RTIME *timep)
 */

static int __rt_timer_read (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME now = rt_timer_read();
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&now,sizeof(now));
    return 0;
}

/*
 * int __rt_timer_tsc(RTIME *tscp)
 */

static int __rt_timer_tsc (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME tsc = rt_timer_tsc();
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&tsc,sizeof(tsc));
    return 0;
}

/*
 * int __rt_timer_ns2ticks(SRTIME *ticksp, SRTIME *nsp)
 */

static int __rt_timer_ns2ticks (struct task_struct *curr, struct pt_regs *regs)

{
    SRTIME ns, ticks;

    __xn_copy_from_user(curr,&ns,(void __user *)__xn_reg_arg2(regs),sizeof(ns));
    ticks = rt_timer_ns2ticks(ns);
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ticks,sizeof(ticks));

    return 0;
}

/*
 * int __rt_timer_ns2tsc(SRTIME *ticksp, SRTIME *nsp)
 */

static int __rt_timer_ns2tsc (struct task_struct *curr, struct pt_regs *regs)

{
    SRTIME ns, ticks;

    __xn_copy_from_user(curr,&ns,(void __user *)__xn_reg_arg2(regs),sizeof(ns));
    ticks = rt_timer_ns2tsc(ns);
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ticks,sizeof(ticks));

    return 0;
}

/*
 * int __rt_timer_ticks2ns(SRTIME *nsp, SRTIME *ticksp)
 */

static int __rt_timer_ticks2ns (struct task_struct *curr, struct pt_regs *regs)

{
    SRTIME ticks, ns;

    __xn_copy_from_user(curr,&ticks,(void __user *)__xn_reg_arg2(regs),sizeof(ticks));
    ns = rt_timer_ticks2ns(ticks);
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ns,sizeof(ns));

    return 0;
}

/*
 * int __rt_timer_tsc2ns(SRTIME *nsp, SRTIME *ticksp)
 */

static int __rt_timer_tsc2ns (struct task_struct *curr, struct pt_regs *regs)

{
    SRTIME ticks, ns;

    __xn_copy_from_user(curr,&ticks,(void __user *)__xn_reg_arg2(regs),sizeof(ticks));
    ns = rt_timer_tsc2ns(ticks);
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ns,sizeof(ns));

    return 0;
}

/*
 * int __rt_timer_inquire(RT_TIMER_INFO *info)
 */

static int __rt_timer_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TIMER_INFO info;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(info)))
	return -EFAULT;

    err = rt_timer_inquire(&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&info,sizeof(info));

    return err;
}

/*
 * int __rt_timer_spin(RTIME *nsp)
 */

static int __rt_timer_spin (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME ns;

    __xn_copy_from_user(curr,&ns,(void __user *)__xn_reg_arg1(regs),sizeof(ns));

    rt_timer_spin(ns);

    return 0;
}

#ifdef CONFIG_XENO_OPT_NATIVE_SEM

/*
 * int __rt_sem_create(RT_SEM_PLACEHOLDER *ph,
 *                     const char *name,
 *                     unsigned icount,
 *                     int mode)
 */

static int __rt_sem_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_SEM_PLACEHOLDER ph;
    unsigned icount;
    int err, mode;
    RT_SEM *sem;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Initial semaphore value. */
    icount = (unsigned)__xn_reg_arg3(regs);
    /* Creation mode. */
    mode = (int)__xn_reg_arg4(regs);

    sem = (RT_SEM *)xnmalloc(sizeof(*sem));

    if (!sem)
	return -ENOMEM;

    err = rt_sem_create(sem,name,icount,mode);

    if (err == 0)
	{
	sem->cpid = curr->pid;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = sem->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(sem);

    return err;
}

/*
 * int __rt_sem_bind(RT_SEM_PLACEHOLDER *ph,
 *                   const char *name,
 *                   RTIME *timeoutp)
 */

static int __rt_sem_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    err = __rt_bind_helper(curr,regs,&ph.opaque,XENO_SEM_MAGIC,NULL);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return err;
}

/*
 * int __rt_sem_delete(RT_SEM_PLACEHOLDER *ph)
 */

static int __rt_sem_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RT_SEM *sem;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

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
 *                RTIME *timeoutp)
 */

static int __rt_sem_p (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RTIME timeout;
    RT_SEM *sem;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)xnregistry_fetch(ph.opaque);

    if (!sem)
	return -ESRCH;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg2(regs),sizeof(timeout));

    return rt_sem_p(sem,timeout);
}

/*
 * int __rt_sem_v(RT_SEM_PLACEHOLDER *ph)
 */

static int __rt_sem_v (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RT_SEM *sem;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)xnregistry_fetch(ph.opaque);

    if (!sem)
	return -ESRCH;

    return rt_sem_v(sem);
}

/*
 * int __rt_sem_broadcast(RT_SEM_PLACEHOLDER *ph)
 */

static int __rt_sem_broadcast (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RT_SEM *sem;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)xnregistry_fetch(ph.opaque);

    if (!sem)
	return -ESRCH;

    return rt_sem_broadcast(sem);
}

/*
 * int __rt_sem_inquire(RT_SEM_PLACEHOLDER *ph,
 *                      RT_SEM_INFO *infop)
 */

static int __rt_sem_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RT_SEM_INFO info;
    RT_SEM *sem;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)xnregistry_fetch(ph.opaque);

    if (!sem)
	return -ESRCH;

    err = rt_sem_inquire(sem,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
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

static int __rt_event_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_EVENT_PLACEHOLDER ph;
    unsigned ivalue;
    RT_EVENT *event;
    int err, mode;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Initial event mask value. */
    ivalue = (unsigned)__xn_reg_arg3(regs);
    /* Creation mode. */
    mode = (int)__xn_reg_arg4(regs);

    event = (RT_EVENT *)xnmalloc(sizeof(*event));

    if (!event)
	return -ENOMEM;

    err = rt_event_create(event,name,ivalue,mode);

    if (err == 0)
	{
	event->cpid = curr->pid;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = event->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(event);

    return err;
}

/*
 * int __rt_event_bind(RT_EVENT_PLACEHOLDER *ph,
 *                     const char *name,
 *                     RTIME *timeoutp)
 */

static int __rt_event_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_EVENT_PLACEHOLDER ph;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    err = __rt_bind_helper(curr,regs,&ph.opaque,XENO_EVENT_MAGIC,NULL);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return err;
}

/*
 * int __rt_event_delete(RT_EVENT_PLACEHOLDER *ph)
 */

static int __rt_event_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT *event;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

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
                       unsigned long mask,
                       unsigned long *mask_r,
                       int mode,
 *                     RTIME *timeoutp)
 */

static int __rt_event_wait (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long mask, mask_r;
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT *event;
    RTIME timeout;
    int mode, err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)) ||
	!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(mask_r)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)xnregistry_fetch(ph.opaque);

    if (!event)
	return -ESRCH;

    mask = (unsigned long)__xn_reg_arg2(regs);
    mode = (int)__xn_reg_arg4(regs);
    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg5(regs),sizeof(timeout));

    err = rt_event_wait(event,mask,&mask_r,mode,timeout);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg3(regs),&mask_r,sizeof(mask_r));

    return err;
}

/*
 * int __rt_event_signal(RT_EVENT_PLACEHOLDER *ph,
 *                       unsigned long mask)
 */

static int __rt_event_signal (struct task_struct *curr, struct pt_regs *regs)

{
    RT_EVENT_PLACEHOLDER ph;
    unsigned long mask;
    RT_EVENT *event;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)xnregistry_fetch(ph.opaque);

    if (!event)
	return -ESRCH;

    mask = (unsigned long)__xn_reg_arg2(regs);

    return rt_event_signal(event,mask);
}

/*
 * int __rt_event_clear(RT_EVENT_PLACEHOLDER *ph,
 *                      unsigned long mask,
 *                      unsigned long *mask_r)
 */

static int __rt_event_clear (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long mask, mask_r;
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT *event;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg3(regs) &&
	!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(mask_r)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)xnregistry_fetch(ph.opaque);

    if (!event)
	return -ESRCH;

    mask = (unsigned long)__xn_reg_arg2(regs);

    err = rt_event_clear(event,mask,&mask_r);

    if (!err && __xn_reg_arg3(regs))
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg3(regs),&mask_r,sizeof(mask_r));

    return err;
}

/*
 * int __rt_event_inquire(RT_EVENT_PLACEHOLDER *ph,
 *                        RT_EVENT_INFO *infop)
 */

static int __rt_event_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT_INFO info;
    RT_EVENT *event;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)xnregistry_fetch(ph.opaque);

    if (!event)
	return -ESRCH;

    err = rt_event_inquire(event,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
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

static int __rt_mutex_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX *mutex;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    mutex = (RT_MUTEX *)xnmalloc(sizeof(*mutex));

    if (!mutex)
	return -ENOMEM;

    err = rt_mutex_create(mutex,name);

    if (err == 0)
	{
	mutex->cpid = curr->pid;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = mutex->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(mutex);

    return err;
}

/*
 * int __rt_mutex_bind(RT_MUTEX_PLACEHOLDER *ph,
 *                     const char *name,
 *                     RTIME *timeoutp)
 */

static int __rt_mutex_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    err = __rt_bind_helper(curr,regs,&ph.opaque,XENO_MUTEX_MAGIC,NULL);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return err;
}

/*
 * int __rt_mutex_delete(RT_MUTEX_PLACEHOLDER *ph)
 */

static int __rt_mutex_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX *mutex;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)xnregistry_fetch(ph.opaque);

    if (!mutex)
	return -ESRCH;

    err = rt_mutex_delete(mutex);

    if (!err && mutex->cpid)
	xnfree(mutex);

    return err;
}

/*
 * int __rt_mutex_lock(RT_MUTEX_PLACEHOLDER *ph,
 *                     RTIME *timeoutp)
 *
 */

static int __rt_mutex_lock (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX *mutex;
    RTIME timeout;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));
    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg2(regs),sizeof(timeout));

    mutex = (RT_MUTEX *)xnregistry_fetch(ph.opaque);

    if (!mutex)
	return -ESRCH;

    return rt_mutex_lock(mutex,timeout);
}

/*
 * int __rt_mutex_unlock(RT_MUTEX_PLACEHOLDER *ph)
 */

static int __rt_mutex_unlock (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX *mutex;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)xnregistry_fetch(ph.opaque);

    if (!mutex)
	return -ESRCH;

    return rt_mutex_unlock(mutex);
}

/*
 * int __rt_mutex_inquire(RT_MUTEX_PLACEHOLDER *ph,
 *                        RT_MUTEX_INFO *infop)
 */

static int __rt_mutex_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX_INFO info;
    RT_MUTEX *mutex;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)xnregistry_fetch(ph.opaque);

    if (!mutex)
	return -ESRCH;

    err = rt_mutex_inquire(mutex,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_XENO_OPT_NATIVE_MUTEX */

#define __rt_mutex_create  __rt_call_not_available
#define __rt_mutex_bind    __rt_call_not_available
#define __rt_mutex_delete  __rt_call_not_available
#define __rt_mutex_lock    __rt_call_not_available
#define __rt_mutex_unlock  __rt_call_not_available
#define __rt_mutex_inquire __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_MUTEX */

#ifdef CONFIG_XENO_OPT_NATIVE_COND

/*
 * int __rt_cond_create(RT_COND_PLACEHOLDER *ph,
 *                      const char *name)
 */

static int __rt_cond_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_COND_PLACEHOLDER ph;
    RT_COND *cond;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    cond = (RT_COND *)xnmalloc(sizeof(*cond));

    if (!cond)
	return -ENOMEM;

    err = rt_cond_create(cond,name);

    if (err == 0)
	{
	cond->cpid = curr->pid;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = cond->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(cond);

    return err;
}

/*
 * int __rt_cond_bind(RT_COND_PLACEHOLDER *ph,
 *                    const char *name,
 *                    RTIME *timeoutp)
 */

static int __rt_cond_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    err = __rt_bind_helper(curr,regs,&ph.opaque,XENO_COND_MAGIC,NULL);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return err;
}

/*
 * int __rt_cond_delete(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    RT_COND *cond;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)xnregistry_fetch(ph.opaque);

    if (!cond)
	return -ESRCH;

    err = rt_cond_delete(cond);

    if (!err && cond->cpid)
	xnfree(cond);

    return err;
}

/*
 * int __rt_cond_wait(RT_COND_PLACEHOLDER *cph,
 *                    RT_MUTEX_PLACEHOLDER *mph,
 *                    RTIME *timeoutp)
 */

static int __rt_cond_wait (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER cph, mph;
    RT_MUTEX *mutex;
    RT_COND *cond;
    RTIME timeout;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(cph)) ||
	!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(mph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&cph,(void __user *)__xn_reg_arg1(regs),sizeof(cph));
    __xn_copy_from_user(curr,&mph,(void __user *)__xn_reg_arg2(regs),sizeof(mph));

    cond = (RT_COND *)xnregistry_fetch(cph.opaque);

    if (!cond)
	return -ESRCH;

    mutex = (RT_MUTEX *)xnregistry_fetch(mph.opaque);

    if (!mutex)
	return -ESRCH;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg3(regs),sizeof(timeout));

    return rt_cond_wait(cond,mutex,timeout);
}

/*
 * int __rt_cond_signal(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_signal (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    RT_COND *cond;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)xnregistry_fetch(ph.opaque);

    if (!cond)
	return -ESRCH;

    return rt_cond_signal(cond);
}

/*
 * int __rt_cond_broadcast(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_broadcast (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    RT_COND *cond;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)xnregistry_fetch(ph.opaque);

    if (!cond)
	return -ESRCH;

    return rt_cond_broadcast(cond);
}

/*
 * int __rt_cond_inquire(RT_COND_PLACEHOLDER *ph,
 *                       RT_COND_INFO *infop)
 */

static int __rt_cond_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    RT_COND_INFO info;
    RT_COND *cond;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)xnregistry_fetch(ph.opaque);

    if (!cond)
	return -ESRCH;

    err = rt_cond_inquire(cond,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_XENO_OPT_NATIVE_COND */

#define __rt_cond_create    __rt_call_not_available
#define __rt_cond_bind      __rt_call_not_available
#define __rt_cond_delete    __rt_call_not_available
#define __rt_cond_wait      __rt_call_not_available
#define __rt_cond_signal    __rt_call_not_available
#define __rt_cond_broadcast __rt_call_not_available
#define __rt_cond_inquire   __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_COND */

#ifdef CONFIG_XENO_OPT_NATIVE_QUEUE

/*
 * int __rt_queue_create(RT_QUEUE_PLACEHOLDER *ph,
 *                       const char *name,
 *                       size_t poolsize,
 *                       size_t qlimit,
 *                       int mode)
 */

static int __rt_queue_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_QUEUE_PLACEHOLDER ph;
    size_t poolsize, qlimit;
    int err, mode;
    RT_QUEUE *q;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Size of memory pool. */
    poolsize = (size_t)__xn_reg_arg3(regs);
    /* Queue limit. */
    qlimit = (size_t)__xn_reg_arg4(regs);
    /* Creation mode. */
    mode = (int)__xn_reg_arg5(regs);

    q = (RT_QUEUE *)xnmalloc(sizeof(*q));

    if (!q)
	return -ENOMEM;

    err = rt_queue_create(q,name,poolsize,qlimit,mode);

    if (err)
	goto free_and_fail;

    q->cpid = curr->pid;

    /* Copy back the registry handle to the ph struct. */
    ph.opaque = q->handle;
    ph.opaque2 = &q->bufpool;
    ph.mapsize = xnheap_size(&q->bufpool);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

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

static int __rt_queue_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    RT_QUEUE *q;
    int err;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    xnlock_get_irqsave(&nklock,s);

    err = __rt_bind_helper(curr,regs,&ph.opaque,XENO_QUEUE_MAGIC,(void **)&q);

    if (err)
	goto unlock_and_exit;

    ph.opaque2 = &q->bufpool;
    ph.mapsize = xnheap_size(&q->bufpool);

    xnlock_put_irqrestore(&nklock,s);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    /* We might need to migrate to secondary mode now for mapping the
       pool memory to user-space; since this syscall is conforming, we
       might have entered it in primary mode. */

    if (xnpod_primary_p())
	xnshadow_relax(0);

    return err;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_queue_delete(RT_QUEUE_PLACEHOLDER *ph)
 */

static int __rt_queue_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    RT_QUEUE *q;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

    if (!q)
	err = -ESRCH;
    else
	{
	err = rt_queue_delete(q); /* Callee will check the queue
				     descriptor for validity again. */
	if (!err && q->cpid)
	    xnfree(q);
	}

    return err;
}

/*
 * int __rt_queue_alloc(RT_QUEUE_PLACEHOLDER *ph,
 *                     size_t size,
 *                     void **bufp)
 */

static int __rt_queue_alloc (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    size_t size;
    RT_QUEUE *q;
    int err = 0;
    void *buf;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(buf)))
	return -EFAULT;

    xnlock_get_irqsave(&nklock,s);

    q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

    if (!q)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    size = (size_t)__xn_reg_arg2(regs);

    buf = rt_queue_alloc(q,size);

    /* Convert the kernel-based address of buf to the equivalent area
       into the caller's address space. */

    if (buf)
	buf = ph.mapbase + xnheap_mapped_offset(&q->bufpool,buf);
    else
	err = -ENOMEM;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg3(regs),&buf,sizeof(buf));

    return err;
}

/*
 * int __rt_queue_free(RT_QUEUE_PLACEHOLDER *ph,
 *                     void *buf)
 */

static int __rt_queue_free (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    void __user *buf;
    RT_QUEUE *q;
    int err;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    buf = (void __user *)__xn_reg_arg2(regs);

    xnlock_get_irqsave(&nklock,s);

    q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

    if (!q)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    /* Convert the caller-based address of buf to the equivalent area
       into the kernel address space. We don't know whether buf is
       valid memory yet, do not dereference it. */

    if (buf)
	{
	buf = xnheap_mapped_address(&q->bufpool,(caddr_t)buf - ph.mapbase);
	err = rt_queue_free(q,buf);
	}
    else
	err = -EINVAL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_queue_send(RT_QUEUE_PLACEHOLDER *ph,
 *                     void *buf,
 *                     size_t size,
 *                     int mode)
 */

static int __rt_queue_send (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    void __user *buf;
    int err, mode;
    RT_QUEUE *q;
    size_t size;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    /* Buffer to send. */
    buf = (void __user *)__xn_reg_arg2(regs);

    /* Message's payload size. */
    size = (size_t)__xn_reg_arg3(regs);

    /* Sending mode. */
    mode = (int)__xn_reg_arg4(regs);

    xnlock_get_irqsave(&nklock,s);

    q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

    if (!q)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    /* Convert the caller-based address of buf to the equivalent area
       into the kernel address space. We don't know whether buf is
       valid memory yet, do not dereference it. */

    if (buf)
	{
	buf = xnheap_mapped_address(&q->bufpool,(caddr_t)buf - ph.mapbase);
	err = rt_queue_send(q,buf,size,mode);
	}
    else
	err = -EINVAL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_queue_recv(RT_QUEUE_PLACEHOLDER *ph,
 *                     void **bufp,
 *                     RTIME *timeoutp)
 */

static int __rt_queue_recv (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    RTIME timeout;
    RT_QUEUE *q;
    void *buf;
    int err;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(buf)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(timeout)))
	return -EFAULT;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg3(regs),sizeof(timeout));

    xnlock_get_irqsave(&nklock,s);

    q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

    if (!q)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    err = (int)rt_queue_recv(q,&buf,timeout);

    /* Convert the caller-based address of buf to the equivalent area
       into the kernel address space. */

    if (err >= 0)
	{
	/* Convert the kernel-based address of buf to the equivalent area
	   into the caller's address space. */
	buf = ph.mapbase + xnheap_mapped_offset(&q->bufpool,buf);
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&buf,sizeof(buf));
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_queue_inquire(RT_QUEUE_PLACEHOLDER *ph,
 *                        RT_QUEUE_INFO *infop)
 */

static int __rt_queue_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    RT_QUEUE_INFO info;
    RT_QUEUE *q;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    q = (RT_QUEUE *)xnregistry_fetch(ph.opaque);

    if (!q)
	return -ESRCH;

    err = rt_queue_inquire(q,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_XENO_OPT_NATIVE_QUEUE */

#define __rt_queue_create    __rt_call_not_available
#define __rt_queue_bind      __rt_call_not_available
#define __rt_queue_delete    __rt_call_not_available
#define __rt_queue_alloc     __rt_call_not_available
#define __rt_queue_free      __rt_call_not_available
#define __rt_queue_send      __rt_call_not_available
#define __rt_queue_recv      __rt_call_not_available
#define __rt_queue_inquire   __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_QUEUE */

#ifdef CONFIG_XENO_OPT_NATIVE_HEAP

/*
 * int __rt_heap_create(RT_HEAP_PLACEHOLDER *ph,
 *                      const char *name,
 *                      size_t heapsize,
 *                      int mode)
 */

static int __rt_heap_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_HEAP_PLACEHOLDER ph;
    size_t heapsize;
    int err, mode;
    RT_HEAP *heap;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Size of heap space. */
    heapsize = (size_t)__xn_reg_arg3(regs);
    /* Creation mode. */
    mode = (int)__xn_reg_arg4(regs);

    heap = (RT_HEAP *)xnmalloc(sizeof(*heap));

    if (!heap)
	return -ENOMEM;

    err = rt_heap_create(heap,name,heapsize,mode);

    if (err)
	goto free_and_fail;

    heap->cpid = curr->pid;

    /* Copy back the registry handle to the ph struct. */
    ph.opaque = heap->handle;
    ph.opaque2 = &heap->heap_base;
    ph.mapsize = xnheap_size(&heap->heap_base);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

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

static int __rt_heap_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    RT_HEAP *heap;
    int err;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    xnlock_get_irqsave(&nklock,s);

    err = __rt_bind_helper(curr,regs,&ph.opaque,XENO_HEAP_MAGIC,(void **)&heap);

    if (err)
	goto unlock_and_exit;

    ph.opaque2 = &heap->heap_base;
    ph.mapsize = xnheap_size(&heap->heap_base);

    xnlock_put_irqrestore(&nklock,s);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    /* We might need to migrate to secondary mode now for mapping the
       heap memory to user-space; since this syscall is conforming, we
       might have entered it in primary mode. */

    if (xnpod_primary_p())
	xnshadow_relax(0);

    return err;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_heap_delete(RT_HEAP_PLACEHOLDER *ph)
 */

static int __rt_heap_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    RT_HEAP *heap;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    heap = (RT_HEAP *)xnregistry_fetch(ph.opaque);

    if (!heap)
	err = -ESRCH;
    else
	{
	err = rt_heap_delete(heap); /* Callee will check the heap
				       descriptor for validity again. */
	if (!err && heap->cpid)
	    xnfree(heap);
	}

    return err;
}

/*
 * int __rt_heap_alloc(RT_HEAP_PLACEHOLDER *ph,
 *                     size_t size,
 *                     RTIME timeout,
 *                     void **bufp)
 */

static int __rt_heap_alloc (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    RT_HEAP *heap;
    RTIME timeout;
    size_t size;
    int err = 0;
    void *buf;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(timeout)))
	return -EFAULT;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg3(regs),sizeof(timeout));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(buf)))
	return -EFAULT;

    xnlock_get_irqsave(&nklock,s);

    heap = (RT_HEAP *)xnregistry_fetch(ph.opaque);

    if (!heap)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    size = (size_t)__xn_reg_arg2(regs);

    err = rt_heap_alloc(heap,size,timeout,&buf);

    /* Convert the kernel-based address of buf to the equivalent area
       into the caller's address space. */

    if (!err)
	buf = ph.mapbase + xnheap_mapped_offset(&heap->heap_base,buf);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg4(regs),&buf,sizeof(buf));

    return err;
}

/*
 * int __rt_heap_free(RT_HEAP_PLACEHOLDER *ph,
 *                    void *buf)
 */

static int __rt_heap_free (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    void __user *buf;
    RT_HEAP *heap;
    int err;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    buf = (void __user *)__xn_reg_arg2(regs);

    xnlock_get_irqsave(&nklock,s);

    heap = (RT_HEAP *)xnregistry_fetch(ph.opaque);

    if (!heap)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    /* Convert the caller-based address of buf to the equivalent area
       into the kernel address space. */

    if (buf)
	{
	buf = xnheap_mapped_address(&heap->heap_base,(caddr_t)buf - ph.mapbase);
	err = rt_heap_free(heap,buf);
	}
    else
	err = -EINVAL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_heap_inquire(RT_HEAP_PLACEHOLDER *ph,
 *                       RT_HEAP_INFO *infop)
 */

static int __rt_heap_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    RT_HEAP_INFO info;
    RT_HEAP *heap;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    heap = (RT_HEAP *)xnregistry_fetch(ph.opaque);

    if (!heap)
	return -ESRCH;

    err = rt_heap_inquire(heap,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
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

void rt_alarm_handler (RT_ALARM *alarm, void *cookie)

{
    /* Wake up all tasks waiting for the alarm. */
    xnsynch_flush(&alarm->synch_base,0);
}

EXPORT_SYMBOL(rt_alarm_handler);

/*
 * int __rt_alarm_create(RT_ALARM_PLACEHOLDER *ph,
 *                       const char *name)
 */

static int __rt_alarm_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM *alarm;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    alarm = (RT_ALARM *)xnmalloc(sizeof(*alarm));

    if (!alarm)
	return -ENOMEM;

    err = rt_alarm_create(alarm,name,&rt_alarm_handler,NULL);

    if (err == 0)
	{
	alarm->cpid = curr->pid;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = alarm->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(alarm);

    return err;
}

/*
 * int __rt_alarm_delete(RT_ALARM_PLACEHOLDER *ph)
 */

static int __rt_alarm_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM *alarm;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

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

static int __rt_alarm_start (struct task_struct *curr, struct pt_regs *regs)

{
    RT_ALARM_PLACEHOLDER ph;
    RTIME value, interval;
    RT_ALARM *alarm;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    alarm = (RT_ALARM *)xnregistry_fetch(ph.opaque);

    if (!alarm)
	return -ESRCH;

    __xn_copy_from_user(curr,&value,(void __user *)__xn_reg_arg2(regs),sizeof(value));
    __xn_copy_from_user(curr,&interval,(void __user *)__xn_reg_arg3(regs),sizeof(interval));

    return rt_alarm_start(alarm,value,interval);
}

/*
 * int __rt_alarm_stop(RT_ALARM_PLACEHOLDER *ph)
 */

static int __rt_alarm_stop (struct task_struct *curr, struct pt_regs *regs)

{
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM *alarm;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    alarm = (RT_ALARM *)xnregistry_fetch(ph.opaque);

    if (!alarm)
	return -ESRCH;

    return rt_alarm_stop(alarm);
}

/*
 * int __rt_alarm_wait(RT_ALARM_PLACEHOLDER *ph)
 */

static int __rt_alarm_wait (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK *task = xeno_current_task();
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM *alarm;
    int err = 0;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    xnlock_get_irqsave(&nklock,s);

    alarm = xeno_h2obj_validate(xnregistry_fetch(ph.opaque),XENO_ALARM_MAGIC,RT_ALARM);

    if (!alarm)
        {
        err = xeno_handle_error(alarm,XENO_ALARM_MAGIC,RT_ALARM);
        goto unlock_and_exit;
        }

    if (xnthread_base_priority(&task->thread_base) != XNCORE_IRQ_PRIO)
	/* Renice the waiter above all regular tasks if needed. */
	xnpod_renice_thread(&task->thread_base,XNCORE_IRQ_PRIO);

    xnsynch_sleep_on(&alarm->synch_base,XN_INFINITE);

    if (xnthread_test_flags(&task->thread_base,XNRMID))
	err = -EIDRM; /* Alarm deleted while pending. */
    else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	err = -EINTR; /* Unblocked.*/

 unlock_and_exit:
    
    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_alarm_inquire(RT_ALARM_PLACEHOLDER *ph,
 *                        RT_ALARM_INFO *infop)
 */

static int __rt_alarm_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM_INFO info;
    RT_ALARM *alarm;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    alarm = (RT_ALARM *)xnregistry_fetch(ph.opaque);

    if (!alarm)
	return -ESRCH;

    err = rt_alarm_inquire(alarm,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
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

int rt_intr_handler (xnintr_t *cookie)

{
    RT_INTR *intr = I_DESC(cookie);

    ++intr->pending;

    if (xnsynch_nsleepers(&intr->synch_base) > 0)
	xnsynch_flush(&intr->synch_base,0);

    if (intr->mode & XN_ISR_PROPAGATE)
	return XN_ISR_PROPAGATE|(intr->mode & XN_ISR_NOENABLE);

    return XN_ISR_HANDLED|(intr->mode & XN_ISR_NOENABLE);
}

EXPORT_SYMBOL(rt_intr_handler);

/*
 * int __rt_intr_create(RT_INTR_PLACEHOLDER *ph,
 *			const char *name,
 *                      unsigned irq,
 *                      int mode)
 */

static int __rt_intr_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_INTR_PLACEHOLDER ph;
    int err, mode;
    RT_INTR *intr;
    unsigned irq;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Interrupt line number. */
    irq = (unsigned)__xn_reg_arg3(regs);

    /* Interrupt control mode. */
    mode = (int)__xn_reg_arg4(regs);

    if (mode & ~(I_NOAUTOENA|I_PROPAGATE))
	return -EINVAL;

    intr = (RT_INTR *)xnmalloc(sizeof(*intr));

    if (!intr)
	return -ENOMEM;

    err = rt_intr_create(intr,name,irq,&rt_intr_handler,NULL,0);

    if (err == 0)
	{
	intr->mode = mode;
	intr->cpid = curr->pid;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = intr->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(intr);

    return err;
}

/*
 * int __rt_intr_bind(RT_INTR_PLACEHOLDER *ph,
 *                    const char *name,
 *                    RTIME *timeoutp)
 */

static int __rt_intr_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_INTR_PLACEHOLDER ph;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    err = __rt_bind_helper(curr,regs,&ph.opaque,XENO_INTR_MAGIC,NULL);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return err;
}

/*
 * int __rt_intr_delete(RT_INTR_PLACEHOLDER *ph)
 */

static int __rt_intr_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_INTR_PLACEHOLDER ph;
    RT_INTR *intr;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

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

static int __rt_intr_wait (struct task_struct *curr, struct pt_regs *regs)

{
    RT_INTR_PLACEHOLDER ph;
    RTIME timeout;
    RT_INTR *intr;
    RT_TASK *task;
    int err = 0;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(timeout)))
	return -EFAULT;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg2(regs),sizeof(timeout));

    if (timeout == TM_NONBLOCK)
	return -EINVAL;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    xnlock_get_irqsave(&nklock,s);

    intr = xeno_h2obj_validate(xnregistry_fetch(ph.opaque),XENO_INTR_MAGIC,RT_INTR);

    if (!intr)
        {
        err = xeno_handle_error(intr,XENO_INTR_MAGIC,RT_INTR);
        goto unlock_and_exit;
        }

    if (!intr->pending)
	{
	task = xeno_current_task();

	if (xnthread_base_priority(&task->thread_base) != XNCORE_IRQ_PRIO)
	    /* Renice the waiter above all regular tasks if needed. */
	    xnpod_renice_thread(&task->thread_base,XNCORE_IRQ_PRIO);

	xnsynch_sleep_on(&intr->synch_base,timeout);
        
	if (xnthread_test_flags(&task->thread_base,XNRMID))
	    err = -EIDRM; /* Interrupt object deleted while pending. */
	else if (xnthread_test_flags(&task->thread_base,XNTIMEO))
	    err = -ETIMEDOUT; /* Timeout.*/
	else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	    err = -EINTR; /* Unblocked.*/
	else
	    err = intr->pending;
	}
    else
	err = intr->pending;

    intr->pending = 0;
    
 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_intr_enable(RT_INTR_PLACEHOLDER *ph)
 */

static int __rt_intr_enable (struct task_struct *curr, struct pt_regs *regs)

{
    RT_INTR_PLACEHOLDER ph;
    RT_INTR *intr;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    intr = (RT_INTR *)xnregistry_fetch(ph.opaque);

    if (!intr)
	return -ESRCH;

    return rt_intr_enable(intr);
}

/*
 * int __rt_intr_disable(RT_INTR_PLACEHOLDER *ph)
 */

static int __rt_intr_disable (struct task_struct *curr, struct pt_regs *regs)

{
    RT_INTR_PLACEHOLDER ph;
    RT_INTR *intr;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    intr = (RT_INTR *)xnregistry_fetch(ph.opaque);

    if (!intr)
	return -ESRCH;

    return rt_intr_disable(intr);
}

/*
 * int __rt_intr_inquire(RT_INTR_PLACEHOLDER *ph,
 *                       RT_INTR_INFO *infop)
 */

static int __rt_intr_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_INTR_PLACEHOLDER ph;
    RT_INTR_INFO info;
    RT_INTR *intr;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    intr = (RT_INTR *)xnregistry_fetch(ph.opaque);

    if (!intr)
	return -ESRCH;

    err = rt_intr_inquire(intr,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
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
 *                      int minor)
 */

static int __rt_pipe_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_PIPE_PLACEHOLDER ph;
    int err, minor;
    size_t poolsize;
    RT_PIPE *pipe;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Device minor. */
    minor = (int)__xn_reg_arg3(regs);

    /* Buffer pool size. */
    poolsize = (size_t)__xn_reg_arg4(regs);

    pipe = (RT_PIPE *)xnmalloc(sizeof(*pipe));

    if (!pipe)
	return -ENOMEM;

    err = rt_pipe_create(pipe,name,minor,poolsize);

    if (err == 0)
	{
	pipe->cpid = curr->pid;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = pipe->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(pipe);

    return err;
}

/*
 * int __rt_pipe_bind(RT_PIPE_PLACEHOLDER *ph,
 *                    const char *name,
 *                    RTIME *timeoutp)
 */

static int __rt_pipe_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_PIPE_PLACEHOLDER ph;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    err = __rt_bind_helper(curr,regs,&ph.opaque,XENO_PIPE_MAGIC,NULL);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return err;
}

/*
 * int __rt_pipe_delete(RT_PIPE_PLACEHOLDER *ph)
 */

static int __rt_pipe_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_PIPE_PLACEHOLDER ph;
    RT_PIPE *pipe;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    pipe = (RT_PIPE *)xnregistry_fetch(ph.opaque);

    if (!pipe)
	return -ESRCH;

    err = rt_pipe_delete(pipe);

    if (!err && pipe->cpid)
	xnfree(pipe);

    return err;
}

/*
 * int __rt_pipe_read(RT_PIPE_PLACEHOLDER *ph,
 *                    void *buf,
 *                    size_t size,
 *                    RTIME timeout)
 */

static int __rt_pipe_read (struct task_struct *curr, struct pt_regs *regs)

{
    RT_PIPE_PLACEHOLDER ph;
    RT_PIPE_MSG *msg;
    RT_PIPE *pipe;
    RTIME timeout;
    size_t size;
    ssize_t err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    pipe = (RT_PIPE *)xnregistry_fetch(ph.opaque);

    if (!pipe)
	return -ESRCH;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg4(regs),sizeof(timeout));

    size = (size_t)__xn_reg_arg3(regs);

    if (size > 0 && !__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),size))
	return -EFAULT;

    err = rt_pipe_receive(pipe,&msg,timeout);

    if (err < 0)
	return err;

    if (size < P_MSGSIZE(msg))
	err = -ENOBUFS;
    else if (P_MSGSIZE(msg) > 0)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),P_MSGPTR(msg),P_MSGSIZE(msg));

    /* Zero-sized messages are allowed, so we still need to free the
       message buffer even if no data copy took place. */

    rt_pipe_free(pipe,msg);

    return err;
}

/*
 * int __rt_pipe_write(RT_PIPE_PLACEHOLDER *ph,
 *                     const void *buf,
 *                     size_t size,
 *                     int mode)
 */

static int __rt_pipe_write (struct task_struct *curr, struct pt_regs *regs)

{
    RT_PIPE_PLACEHOLDER ph;
    RT_PIPE_MSG *msg;
    RT_PIPE *pipe;
    size_t size;
    ssize_t err;
    int mode;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    pipe = (RT_PIPE *)xnregistry_fetch(ph.opaque);

    if (!pipe)
	return -ESRCH;

    size = (size_t)__xn_reg_arg3(regs);
    mode = (int)__xn_reg_arg4(regs);

    if (size == 0)
	/* Try flushing the streaming buffer in any case. */
	return rt_pipe_send(pipe,NULL,0,mode);

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),size))
	return -EFAULT;

    msg = rt_pipe_alloc(pipe,size);

    if (!msg)
	return -ENOMEM;

    __xn_copy_from_user(curr,P_MSGPTR(msg),(void __user *)__xn_reg_arg2(regs),size);

    err = rt_pipe_send(pipe,msg,size,mode);

    if (err != size)
	/* If the operation failed, we need to free the message buffer
	   by ourselves. */
       rt_pipe_free(pipe,msg);

    return err;
}

/*
 * int __rt_pipe_stream(RT_PIPE_PLACEHOLDER *ph,
 *                      const void *buf,
 *                      size_t size)
 */

static int __rt_pipe_stream (struct task_struct *curr, struct pt_regs *regs)

{
    RT_PIPE_PLACEHOLDER ph;
    RT_PIPE_MSG *msg;
    char tmp_buf[64];
    RT_PIPE *pipe;
    size_t size;
    ssize_t err;
    void *buf;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    pipe = (RT_PIPE *)xnregistry_fetch(ph.opaque);

    if (!pipe)
	return -ESRCH;

    size = (size_t)__xn_reg_arg3(regs);

    if (size == 0)
	/* Try flushing the streaming buffer in any case. */
	return rt_pipe_stream(pipe,NULL,0);

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),size))
	return -EFAULT;

    /* Try using a local fast buffer if the sent data fits into it. */
 
    if (size <= sizeof(tmp_buf))
	{
	msg = NULL;
	buf = tmp_buf;
	}
    else
	{
       msg = rt_pipe_alloc(pipe,size);

	if (!msg)
	    return -ENOMEM;

	buf = P_MSGPTR(msg);
	}

    __xn_copy_from_user(curr,buf,(void __user *)__xn_reg_arg2(regs),size);

    err = rt_pipe_stream(pipe,buf,size);

    if (msg)
       rt_pipe_free(pipe,msg);

    return err;
}

/*
 * int __rt_pipe_flush(RT_PIPE_PLACEHOLDER *ph)
 */

static int __rt_pipe_flush (struct task_struct *curr, struct pt_regs *regs)

{
    RT_PIPE_PLACEHOLDER ph;
    RT_PIPE *pipe;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    pipe = (RT_PIPE *)xnregistry_fetch(ph.opaque);

    return pipe ? rt_pipe_flush(pipe) : -ESRCH;
}

#else /* !CONFIG_XENO_OPT_NATIVE_PIPE */

#define __rt_pipe_create   __rt_call_not_available
#define __rt_pipe_bind     __rt_call_not_available
#define __rt_pipe_delete   __rt_call_not_available
#define __rt_pipe_read     __rt_call_not_available
#define __rt_pipe_write    __rt_call_not_available
#define __rt_pipe_stream   __rt_call_not_available
#define __rt_pipe_flush    __rt_call_not_available

#endif /* CONFIG_XENO_OPT_NATIVE_PIPE */

/*
 * int __rt_misc_get_io_region(unsigned long start,
 *                             unsigned long len,
 *                             const char *label)
 */

static int __rt_misc_get_io_region (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long start, len;
    char label[64];

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(label)))
	return -EFAULT;

    __xn_strncpy_from_user(curr,label,(const char __user *)__xn_reg_arg3(regs),sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';

    start = __xn_reg_arg1(regs);
    len = __xn_reg_arg2(regs);

    return request_region(start,len,label) ? 0 : -EBUSY;
}

/*
 * int __rt_misc_put_io_region(unsigned long start,
 *                             unsigned long len)
 */

static int __rt_misc_put_io_region (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long start, len;

    start = __xn_reg_arg1(regs);
    len = __xn_reg_arg2(regs);
    release_region(start,len);

    return 0;
}

static  __attribute__((unused))
int __rt_call_not_available (struct task_struct *curr, struct pt_regs *regs)
{
    return -ENOSYS;
}

static xnsysent_t __systab[] = {
    [__native_task_create ] = { &__rt_task_create, __xn_exec_init },
    [__native_task_bind ] = { &__rt_task_bind, __xn_exec_conforming },
    [__native_task_start ] = { &__rt_task_start, __xn_exec_any },
    [__native_task_suspend ] = { &__rt_task_suspend, __xn_exec_conforming },
    [__native_task_resume ] = { &__rt_task_resume, __xn_exec_any },
    [__native_task_delete ] = { &__rt_task_delete, __xn_exec_conforming },
    [__native_task_yield ] = { &__rt_task_yield, __xn_exec_primary },
    [__native_task_set_periodic ] = { &__rt_task_set_periodic, __xn_exec_conforming },
    [__native_task_wait_period ] = { &__rt_task_wait_period, __xn_exec_primary },
    [__native_task_set_priority ] = { &__rt_task_set_priority, __xn_exec_any },
    [__native_task_sleep ] = { &__rt_task_sleep, __xn_exec_primary },
    [__native_task_sleep_until ] = { &__rt_task_sleep_until, __xn_exec_primary },
    [__native_task_unblock ] = { &__rt_task_unblock, __xn_exec_any },
    [__native_task_inquire ] = { &__rt_task_inquire, __xn_exec_any },
    [__native_task_notify ] = { &__rt_task_notify, __xn_exec_any },
    [__native_task_set_mode ] = { &__rt_task_set_mode, __xn_exec_primary },
    [__native_task_self ] = { &__rt_task_self, __xn_exec_any },
    [__native_task_slice ] = { &__rt_task_slice, __xn_exec_any },
    [__native_task_send ] = { &__rt_task_send, __xn_exec_primary },
    [__native_task_receive ] = { &__rt_task_receive, __xn_exec_primary },
    [__native_task_reply ] = { &__rt_task_reply, __xn_exec_primary },
    [__native_timer_set_mode ] = { &__rt_timer_set_mode, __xn_exec_lostage|__xn_exec_switchback },
    [__native_unimp_22 ] = { &__rt_call_not_available, __xn_exec_any },
    [__native_timer_read ] = { &__rt_timer_read, __xn_exec_any },
    [__native_timer_tsc ] = { &__rt_timer_tsc, __xn_exec_any },
    [__native_timer_ns2ticks ] = { &__rt_timer_ns2ticks, __xn_exec_any },
    [__native_timer_ticks2ns ] = { &__rt_timer_ticks2ns, __xn_exec_any },
    [__native_timer_inquire ] = { &__rt_timer_inquire, __xn_exec_any },
    [__native_timer_spin ] = { &__rt_timer_spin, __xn_exec_any },
    [__native_sem_create ] = { &__rt_sem_create, __xn_exec_any },
    [__native_sem_bind ] = { &__rt_sem_bind, __xn_exec_conforming },
    [__native_sem_delete ] = { &__rt_sem_delete, __xn_exec_any },
    [__native_sem_p ] = { &__rt_sem_p, __xn_exec_primary },
    [__native_sem_v ] = { &__rt_sem_v, __xn_exec_any },
    [__native_sem_broadcast ] = { &__rt_sem_broadcast, __xn_exec_any },
    [__native_sem_inquire ] = { &__rt_sem_inquire, __xn_exec_any },
    [__native_event_create ] = { &__rt_event_create, __xn_exec_any },
    [__native_event_bind ] = { &__rt_event_bind, __xn_exec_conforming },
    [__native_event_delete ] = { &__rt_event_delete, __xn_exec_any },
    [__native_event_wait ] = { &__rt_event_wait, __xn_exec_primary },
    [__native_event_signal ] = { &__rt_event_signal, __xn_exec_any },
    [__native_event_clear ] = { &__rt_event_clear, __xn_exec_any },
    [__native_event_inquire ] = { &__rt_event_inquire, __xn_exec_any },
    [__native_mutex_create ] = { &__rt_mutex_create, __xn_exec_any },
    [__native_mutex_bind ] = { &__rt_mutex_bind, __xn_exec_conforming },
    [__native_mutex_delete ] = { &__rt_mutex_delete, __xn_exec_any },
    [__native_mutex_lock ] = { &__rt_mutex_lock, __xn_exec_primary },
    [__native_mutex_unlock ] = { &__rt_mutex_unlock, __xn_exec_primary },
    [__native_mutex_inquire ] = { &__rt_mutex_inquire, __xn_exec_any },
    [__native_cond_create ] = { &__rt_cond_create, __xn_exec_any },
    [__native_cond_bind ] = { &__rt_cond_bind, __xn_exec_conforming },
    [__native_cond_delete ] = { &__rt_cond_delete, __xn_exec_any },
    [__native_cond_wait ] = { &__rt_cond_wait, __xn_exec_primary },
    [__native_cond_signal ] = { &__rt_cond_signal, __xn_exec_any },
    [__native_cond_broadcast ] = { &__rt_cond_broadcast, __xn_exec_any },
    [__native_cond_inquire ] = { &__rt_cond_inquire, __xn_exec_any },
    [__native_queue_create ] = { &__rt_queue_create, __xn_exec_lostage },
    [__native_queue_bind ] = { &__rt_queue_bind, __xn_exec_conforming },
    [__native_queue_delete ] = { &__rt_queue_delete, __xn_exec_lostage },
    [__native_queue_alloc ] = { &__rt_queue_alloc, __xn_exec_any },
    [__native_queue_free ] = { &__rt_queue_free, __xn_exec_any },
    [__native_queue_send ] = { &__rt_queue_send, __xn_exec_any },
    [__native_queue_recv ] = { &__rt_queue_recv, __xn_exec_primary },
    [__native_queue_inquire ] = { &__rt_queue_inquire, __xn_exec_any },
    [__native_heap_create ] = { &__rt_heap_create, __xn_exec_lostage },
    [__native_heap_bind ] = { &__rt_heap_bind, __xn_exec_conforming },
    [__native_heap_delete ] = { &__rt_heap_delete, __xn_exec_lostage },
    [__native_heap_alloc ] = { &__rt_heap_alloc, __xn_exec_conforming },
    [__native_heap_free ] = { &__rt_heap_free, __xn_exec_any },
    [__native_heap_inquire ] = { &__rt_heap_inquire, __xn_exec_any },
    [__native_alarm_create ] = { &__rt_alarm_create, __xn_exec_any },
    [__native_alarm_delete ] = { &__rt_alarm_delete, __xn_exec_any },
    [__native_alarm_start ] = { &__rt_alarm_start, __xn_exec_any },
    [__native_alarm_stop ] = { &__rt_alarm_stop, __xn_exec_any },
    [__native_alarm_wait ] = { &__rt_alarm_wait, __xn_exec_primary },
    [__native_alarm_inquire ] = { &__rt_alarm_inquire, __xn_exec_any },
    [__native_intr_create ] = { &__rt_intr_create, __xn_exec_any },
    [__native_intr_bind ] = { &__rt_intr_bind, __xn_exec_conforming },
    [__native_intr_delete ] = { &__rt_intr_delete, __xn_exec_any },
    [__native_intr_wait ] = { &__rt_intr_wait, __xn_exec_primary },
    [__native_intr_enable ] = { &__rt_intr_enable, __xn_exec_any },
    [__native_intr_disable ] = { &__rt_intr_disable, __xn_exec_any },
    [__native_intr_inquire ] = { &__rt_intr_inquire, __xn_exec_any },
    [__native_pipe_create ] = { &__rt_pipe_create, __xn_exec_lostage },
    [__native_pipe_bind ] = { &__rt_pipe_bind, __xn_exec_conforming },
    [__native_pipe_delete ] = { &__rt_pipe_delete, __xn_exec_any },
    [__native_pipe_read ] = { &__rt_pipe_read, __xn_exec_primary },
    [__native_pipe_write ] = { &__rt_pipe_write, __xn_exec_any },
    [__native_pipe_stream ] = { &__rt_pipe_stream, __xn_exec_any },
    [__native_pipe_flush ] = { &__rt_pipe_flush, __xn_exec_any },
    [__native_misc_get_io_region ] = { &__rt_misc_get_io_region, __xn_exec_lostage },
    [__native_misc_put_io_region ] = { &__rt_misc_put_io_region, __xn_exec_lostage },
    [__native_timer_ns2tsc ] = { &__rt_timer_ns2tsc, __xn_exec_any },
    [__native_timer_tsc2ns ] = { &__rt_timer_tsc2ns, __xn_exec_any },
};

static void __shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == XENO_SKIN_MAGIC &&
	testbits(thread->status,XNSHADOW))
	xnshadow_unmap(thread);
}

int __native_syscall_init (void)

{
    __muxid =
	xnshadow_register_interface("native",
				    XENO_SKIN_MAGIC,
				    sizeof(__systab) / sizeof(__systab[0]),
				    __systab,
				    NULL);
    if (__muxid < 0)
	return -ENOSYS;

    xnpod_add_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    
    return 0;
}

void __native_syscall_cleanup (void)

{
    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    xnshadow_unregister_interface(__muxid);
}
