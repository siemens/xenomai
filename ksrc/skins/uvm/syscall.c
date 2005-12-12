/*
 * Copyright (C) 2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <xenomai/nucleus/pod.h>
#include <xenomai/nucleus/heap.h>
#include <xenomai/nucleus/shadow.h>
#include <xenomai/nucleus/synch.h>
#include <xenomai/asm-uvm/syscall.h>
#include <xenomai/asm-uvm/uvm.h>

static int __vm_muxid;

static xnsynch_t __vm_thread_irqsync;

static inline xnthread_t *__vm_find_thread (struct task_struct *curr, void *khandle)

{
    xnthread_t *thread;

    if (!khandle)
	return NULL;

    thread = (xnthread_t *)khandle;

    if (xnthread_get_magic(thread) != UVM_SKIN_MAGIC)
	return NULL;

    return thread;
}

static int __vm_shadow_helper (struct task_struct *curr,
				struct pt_regs *regs,
				xncompletion_t __user *u_completion)
{
    char name[XNOBJECT_NAME_LEN];
    xnthread_t *thread;

    if (curr->policy != SCHED_FIFO)
	return -EPERM;

    if (__xn_reg_arg2(regs) &&
	!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(thread)))
	return -EFAULT;

    thread = (xnthread_t *)xnmalloc(sizeof(*thread));

    if (!thread)
	return -ENOMEM;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(name)))
	    {
	    xnfree(thread);
	    return -EFAULT;
	    }

	__xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg1(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	strncpy(curr->comm,name,sizeof(curr->comm));
	curr->comm[sizeof(curr->comm) - 1] = '\0';
	}
    else
	{
	strncpy(name,curr->comm,sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}

    if (xnpod_init_thread(thread,
			  name,
			  curr->rt_priority,
			  XNFPU|XNSHADOW|XNSHIELD,
			  0) != 0)
	{
	/* Assume this is the only possible failure. */
	xnfree(thread);
	return -ENOMEM;
	}

    xnthread_set_magic(thread,UVM_SKIN_MAGIC);
    
    /* We don't want some funny guy to rip the new TCB off while two
       user-space threads are being synchronized on it, so enter a
       critical section. Do *not* take the big lock here: this is
       useless since deleting a thread through an inter-CPU request
       requires the target CPU to accept IPIs. */

    if (__xn_reg_arg2(regs))
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&thread,sizeof(thread));

    xnthread_extended_info(thread) = (void *)__xn_reg_arg3(regs);

    return xnshadow_map(thread,u_completion);
}

static int __vm_thread_shadow (struct task_struct *curr, struct pt_regs *regs)
{
    return __vm_shadow_helper(curr,regs,NULL);
}

static int __vm_thread_create (struct task_struct *curr, struct pt_regs *regs)

{
    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(xncompletion_t)))
	return -EFAULT;

    return __vm_shadow_helper(curr,regs,(xncompletion_t __user *)__xn_reg_arg4(regs));
}

static int __vm_thread_start (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread;
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    thread = __vm_find_thread(curr,(void *)__xn_reg_arg1(regs));

    if (!thread)
	{
	err = -ESRCH;
	goto out;
	}

    err = xnpod_start_thread(thread,0,0,XNPOD_ALL_CPUS,NULL,NULL);

 out:

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static int __vm_timer_read (struct task_struct *curr, struct pt_regs *regs)

{
    nanotime_t t;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(t)))
	return -EFAULT;

    t = xnpod_get_time();
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&t,sizeof(t));

    return 0;
}

static int __vm_timer_tsc (struct task_struct *curr, struct pt_regs *regs)

{
    nanotime_t t;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(t)))
	return -EFAULT;

    t = xnarch_get_cpu_tsc();
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&t,sizeof(t));

    return 0;
}

static int __vm_timer_start (struct task_struct *curr, struct pt_regs *regs)

{
    nanotime_t nstick;

    __xn_copy_from_user(curr,&nstick,(void __user *)__xn_reg_arg1(regs),sizeof(nstick));

    if (testbits(nkpod->status,XNTIMED))
	{
	if ((nstick == 0 && xnpod_get_tickval() == 1) ||
	    (nstick != 0 && xnpod_get_tickval() == nstick))
	    return 0;

	xnpod_stop_timer();
	}

    return xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);
}

static int __vm_timer_stop (struct task_struct *curr, struct pt_regs *regs)

{
    xnpod_stop_timer();
    return 0;
}

static int __vm_thread_set_periodic (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread = xnshadow_thread(curr);	/* Can't be NULL. */
    nanotime_t idate, period;

    __xn_copy_from_user(curr,&idate,(void __user *)__xn_reg_arg1(regs),sizeof(idate));
    __xn_copy_from_user(curr,&period,(void __user *)__xn_reg_arg2(regs),sizeof(period));

    return xnpod_set_thread_periodic(thread,idate,period);
}

static int __vm_thread_wait_period (struct task_struct *curr, struct pt_regs *regs)

{
    return xnpod_wait_thread_period();
}

static int __vm_thread_hold (struct task_struct *curr, struct pt_regs *regs)

{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    /* Raise the 'irq pending' flag. */
    __xn_put_user(curr,1,(unsigned long __user *)__xn_reg_arg1(regs));

    xnsynch_sleep_on(&__vm_thread_irqsync,XN_INFINITE);

    if (xnthread_test_flags(xnpod_current_thread(),XNBREAK))
	err = -EINTR; /* Unblocked.*/
    else if (xnthread_test_flags(xnpod_current_thread(),XNRMID))
	err = -EIDRM; /* Sync deleted.*/

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static int __vm_thread_release (struct task_struct *curr, struct pt_regs *regs)

{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    __xn_put_user(curr,0,(unsigned long __user *)__xn_reg_arg1(regs)); /* Clear the irqlock flag */

    if (xnsynch_flush(&__vm_thread_irqsync,XNBREAK) == XNSYNCH_RESCHED)
	xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

static int __vm_thread_idle (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread = xnpod_current_thread();
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    /* Emulate sti() for the UVM before returning to idle mode. */
    __xn_put_user(curr,0,(unsigned long __user *)__xn_reg_arg1(regs)); /* Clear the irqlock flag */

    if (xnsynch_nsleepers(&__vm_thread_irqsync) > 0)
	xnsynch_flush(&__vm_thread_irqsync,XNBREAK);

    xnpod_suspend_thread(thread,XNSUSP,XN_INFINITE,NULL);

    if (xnthread_test_flags(thread,XNBREAK))
	err = -EINTR; /* Unblocked.*/

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static int __vm_thread_activate (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *prev, *next;
    int err = -ESRCH;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    next = __vm_find_thread(curr,(void *)__xn_reg_arg1(regs));

    if (!next)
	goto out;

    prev = __vm_find_thread(curr,(void *)__xn_reg_arg2(regs));

    if (!prev)
	goto out;

    if (!testbits(next->status,XNSTARTED))
	{
	/* First, make sure it won't preempt us. */
	xnpod_suspend_thread(next,XNSUSP,XN_INFINITE,NULL);
	err = xnpod_start_thread(next,0,0,XNPOD_ALL_CPUS,NULL,NULL);
	}

    xnpod_resume_thread(next,XNSUSP);

    xnpod_suspend_thread(prev,XNSUSP,XN_INFINITE,NULL);

    err = 0;

    if (prev == xnpod_current_thread())
	{
	if (xnthread_test_flags(prev,XNBREAK))
	    err = -EINTR; /* Unblocked.*/
	}
    else
	xnpod_schedule();

 out:

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static int __vm_thread_cancel (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *dead, *next;
    int err = -ESRCH;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (__xn_reg_arg1(regs))
	{
	dead = __vm_find_thread(curr,(void *)__xn_reg_arg1(regs));

	if (!dead)
	    goto out;
	}
    else
	dead = xnshadow_thread(curr);

    if (__xn_reg_arg2(regs))
	{
	next = __vm_find_thread(curr,(void *)__xn_reg_arg2(regs));

	if (!next)
	    goto out;

	xnpod_resume_thread(next,XNSUSP);
	}

    err = 0;

    xnpod_delete_thread(dead);

out:

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static void __shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == UVM_SKIN_MAGIC)
	{
	xnshadow_unmap(thread);
	xnfree(thread);
	}
}

static xnsysent_t __systab[] = {
    [__uvm_thread_shadow] = { &__vm_thread_shadow, __xn_exec_init },
    [__uvm_thread_create] = { &__vm_thread_create, __xn_exec_init },
    [__uvm_thread_start] = { &__vm_thread_start, __xn_exec_any },
    [__uvm_thread_set_periodic] = { &__vm_thread_set_periodic, __xn_exec_primary },
    [__uvm_thread_wait_period] = { &__vm_thread_wait_period, __xn_exec_primary },
    [__uvm_thread_idle] = { &__vm_thread_idle, __xn_exec_primary },
    [__uvm_thread_cancel] = { &__vm_thread_cancel, __xn_exec_primary },
    [__uvm_thread_activate] = { &__vm_thread_activate, __xn_exec_primary },
    [__uvm_thread_hold] = { &__vm_thread_hold, __xn_exec_primary },
    [__uvm_thread_release] = { &__vm_thread_release, __xn_exec_any },
    [__uvm_timer_read] = { &__vm_timer_read, __xn_exec_any  },
    [__uvm_timer_tsc] = { &__vm_timer_tsc, __xn_exec_any  },
    [__uvm_timer_start] = { &__vm_timer_start, __xn_exec_lostage  },
    [__uvm_timer_stop] = { &__vm_timer_stop, __xn_exec_lostage  },
};

int __uvm_syscall_init (void)

{
    __vm_muxid =
	xnshadow_register_interface("uvm",
				    UVM_SKIN_MAGIC,
				    sizeof(__systab) / sizeof(__systab[0]),
				    __systab,
				    NULL);
    if (__vm_muxid < 0)
	return -ENOSYS;

    xnpod_add_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    xnsynch_init(&__vm_thread_irqsync,XNSYNCH_FIFO);
    
    return 0;
}

void __uvm_syscall_cleanup (void)

{
    if (xnsynch_destroy(&__vm_thread_irqsync) == XNSYNCH_RESCHED)
	xnpod_schedule();

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    xnshadow_unregister_interface(__vm_muxid);
}
