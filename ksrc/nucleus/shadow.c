/*!\file shadow.c
 * \brief Real-time shadow services.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003,2004,2005,2006 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 The RTAI project <http://www.rtai.org>
 * Copyright (C) 2004 The HYADES project <http://www.hyades-itea.org>
 * Copyright (C) 2005 The Xenomai project <http://www.Xenomai.org>
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
 *
 * \ingroup shadow
 */

/*!
 * \ingroup nucleus
 * \defgroup shadow Real-time shadow services.
 *
 * Real-time shadow services.
 *
 *@{*/

#define XENO_SHADOW_MODULE 1

#include <stdarg.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <asm/signal.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/synch.h>
#include <nucleus/module.h>
#include <nucleus/shadow.h>
#include <nucleus/core.h>
#include <nucleus/ltt.h>
#include <nucleus/jhash.h>
#include <nucleus/ppd.h>
#include <asm/xenomai/features.h>

int nkthrptd;

int nkerrptd;

struct xnskentry muxtable[XENOMAI_MUX_NR];

static struct __gatekeeper {

    struct task_struct *server;
    wait_queue_head_t waitq;
    struct linux_semaphore sync;
    xnthread_t *thread;

} gatekeeper[XNARCH_NR_CPUS];

static int lostage_apc;

static struct __lostagerq {

    int in, out;

    struct {
#define LO_START_REQ  0
#define LO_WAKEUP_REQ 1
#define LO_RENICE_REQ 2
#define LO_SIGGRP_REQ 3
#define LO_SIGTHR_REQ 4
        int type;
        struct task_struct *task;
        int arg;
#define LO_MAX_REQUESTS 64      /* Must be a ^2 */
    } req[LO_MAX_REQUESTS];

} lostagerq[XNARCH_NR_CPUS];

#define get_switch_lock_owner() \
switch_lock_owner[task_cpu(current)]

#define set_switch_lock_owner(t) \
do { \
   switch_lock_owner[task_cpu(t)] = t; \
} while(0)

static struct task_struct *switch_lock_owner[XNARCH_NR_CPUS];

static xnqueue_t *xnshadow_ppd_hash;
#define XNSHADOW_PPD_HASH_SIZE 13

void xnpod_declare_iface_proc(struct xnskentry *iface);

void xnpod_discard_iface_proc(struct xnskentry *iface);

union xnshadow_ppd_hkey {
    struct mm_struct *mm;
    uint32_t val;
};

/* ppd holder with the same mm collide and are stored contiguously in the same
   bucket, so that they can all be destroyed with only one hash lookup by
   xnshadow_ppd_remove_mm. */
static unsigned
xnshadow_ppd_lookup_inner(xnqueue_t **pq,
                          xnshadow_ppd_t **pholder,
                          xnshadow_ppd_key_t *pkey)
{
    union xnshadow_ppd_hkey key = { .mm = pkey->mm };
    unsigned bucket = jhash2(&key.val, sizeof(key)/sizeof(uint32_t), 0);
    xnshadow_ppd_t *ppd;
    xnholder_t *holder;

    *pq = &xnshadow_ppd_hash[bucket % XNSHADOW_PPD_HASH_SIZE];
    holder = getheadq(*pq);

    if (!holder)
        {
        *pholder = NULL;
        return 0;
        }
    
    do 
        {
        ppd = link2ppd(holder);
        holder = nextq(*pq, holder);
        }
    while (holder &&
           (ppd->key.mm < pkey->mm ||
            (ppd->key.mm == pkey->mm && ppd->key.muxid < pkey->muxid)));

    if (ppd->key.mm == pkey->mm && ppd->key.muxid == pkey->muxid)
        {
        /* found it, return it. */
        *pholder = ppd;
        return 1;
        }

    /* not found, return successor for insertion. */
    if (ppd->key.mm < pkey->mm ||
        (ppd->key.mm == pkey->mm && ppd->key.muxid < pkey->muxid))
        *pholder = holder ? link2ppd(holder) : NULL;
    else
        *pholder = ppd;

    return 0;
}

static void xnshadow_ppd_insert(xnshadow_ppd_t *holder)
{
    xnshadow_ppd_t *next;
    xnqueue_t *q;
    unsigned found;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);
    found = xnshadow_ppd_lookup_inner(&q, &next, &holder->key);
    BUG_ON(found);
    inith(&holder->link);
    if (next)
        insertq(q, &next->link, &holder->link);
    else
        appendq(q, &holder->link);
    xnlock_put_irqrestore(&nklock, s);
}

/* will be called by skin code, nklock locked irqs off. */
static xnshadow_ppd_t *xnshadow_ppd_lookup(unsigned muxid, struct mm_struct *mm)
{
    xnshadow_ppd_t *holder;
    xnshadow_ppd_key_t key;
    unsigned found;
    xnqueue_t *q;

    key.muxid = muxid;
    key.mm = mm;
    found = xnshadow_ppd_lookup_inner(&q, &holder, &key);

    if (!found)
        return NULL;

    return holder;
}

static void xnshadow_ppd_remove(xnshadow_ppd_t *holder)
{
    unsigned found;
    xnqueue_t *q;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);
    found = xnshadow_ppd_lookup_inner(&q, &holder, &holder->key);

    if (found)
        removeq(q, &holder->link);

    xnlock_put_irqrestore(&nklock, s);
}

static inline void xnshadow_ppd_remove_mm(struct mm_struct *mm,
                                          void (*destructor)(xnshadow_ppd_t *))
{
    xnshadow_ppd_key_t key;
    xnshadow_ppd_t *ppd;
    xnholder_t *holder;
    xnqueue_t *q;
    spl_t s;

    key.muxid = 0;
    key.mm = mm;
    xnlock_get_irqsave(&nklock, s);
    xnshadow_ppd_lookup_inner(&q, &ppd, &key);

    while (ppd && ppd->key.mm == mm)
        {
        holder = nextq(q, &ppd->link);
        removeq(q, &ppd->link);
        xnlock_put_irqrestore(&nklock, s);
        /* releasing nklock is safe here, if we assume that no insertion for the
           same mm will take place while we are running xnpod_remove_mm. */
        destructor(ppd);

        ppd = holder ? link2ppd(holder) : NULL;
        xnlock_get_irqsave(&nklock, s);
        }

    xnlock_put_irqrestore(&nklock, s);
}

static inline void request_syscall_restart(xnthread_t *thread,
                                           struct pt_regs *regs)
{
    if (testbits(thread->status, XNKICKED)) {
        if (__xn_interrupted_p(regs))
            __xn_error_return(regs, -ERESTARTSYS);

        clrbits(thread->status, XNKICKED);
    }

    /* Relaxing due to a fault will trigger a notification from the
       trap handler when applicable, so we don't otherwise notify upon
       signal receipt, since testing syscall return values for -EINTR
       is still possible to detect such situation. */

    xnshadow_relax(0);
}

static inline void set_linux_task_priority(struct task_struct *p, int prio)
{
    if (rthal_setsched_root(p, SCHED_FIFO, prio) < 0)
        printk(KERN_WARNING
               "Xenomai: invalid Linux priority level: %d, task=%s\n", prio,
               p->comm);
}

static inline void lock_timers(void)
{
    xnarch_atomic_inc(&nkpod->timerlck);
    setbits(nkpod->status, XNTLOCK);
}

static inline void unlock_timers(void)
{
    if (xnarch_atomic_dec_and_test(&nkpod->timerlck))
        clrbits(nkpod->status, XNTLOCK);
}

#ifdef CONFIG_XENO_OPT_ISHIELD

static rthal_pipeline_stage_t irq_shield;

static cpumask_t shielded_cpus, unshielded_cpus;

static rthal_rwlock_t shield_lock = RTHAL_RW_LOCK_UNLOCKED;

static inline void engage_irq_shield(void)
{
    unsigned long flags;
    rthal_declare_cpuid;

    rthal_lock_cpu(flags);

    if (xnarch_cpu_test_and_set(cpuid, shielded_cpus))
        goto unmask_and_exit;

    rthal_read_lock(&shield_lock);

    xnarch_cpu_clear(cpuid, unshielded_cpus);

    xnarch_lock_xirqs(&irq_shield, cpuid);

    rthal_read_unlock(&shield_lock);

  unmask_and_exit:

    rthal_unlock_cpu(flags);
}

static void disengage_irq_shield(void)
{
    unsigned long flags;
    rthal_declare_cpuid;

    rthal_lock_cpu(flags);

    if (xnarch_cpu_test_and_set(cpuid, unshielded_cpus))
        goto unmask_and_exit;

    rthal_write_lock(&shield_lock);

    xnarch_cpu_clear(cpuid, shielded_cpus);

    /* We want the shield to be either engaged on all CPUs (i.e. if at
       least one CPU asked for shielding), or disengaged on all
       (i.e. if no CPU asked for shielding). */

    if (!cpus_empty(shielded_cpus)) {
        rthal_write_unlock(&shield_lock);
        goto unmask_and_exit;
    }

    /* At this point we know that we are the last CPU to disengage the
       shield, so we just unlock the external IRQs for all CPUs, and
       trigger an IPI on everyone but self to make sure that the
       remote interrupt logs will be played. We also forcibly unstall
       the shield stage on the local CPU in order to flush it the same
       way. */

    xnarch_unlock_xirqs(&irq_shield, cpuid);

#ifdef CONFIG_SMP
    {
        cpumask_t other_cpus = xnarch_cpu_online_map;
        xnarch_cpu_clear(cpuid, other_cpus);
        rthal_send_ipi(RTHAL_SERVICE_IPI1, other_cpus);
    }
#endif /* CONFIG_SMP */

    rthal_write_unlock(&shield_lock);

    rthal_stage_irq_enable(&irq_shield);

  unmask_and_exit:

    rthal_unlock_cpu(flags);
}

static inline void reset_shield(xnthread_t *thread)
{
    if (testbits(thread->status, XNSHIELD))
        engage_irq_shield();
    else
        disengage_irq_shield();
}

static void shield_handler(unsigned irq, void *cookie)
{
#ifdef CONFIG_SMP
    if (irq != RTHAL_SERVICE_IPI1)
#endif /* CONFIG_SMP */
        rthal_propagate_irq(irq);
}

static inline void do_shield_domain_entry(void)
{
    xnarch_grab_xirqs(&shield_handler);
}

RTHAL_DECLARE_DOMAIN(shield_domain_entry);

void xnshadow_reset_shield(void)
{
    xnthread_t *thread = xnshadow_thread(current);

    if (!thread)
        return;                 /* uh?! */

    reset_shield(thread);
}

#endif /* CONFIG_XENO_OPT_ISHIELD */

static void lostage_handler(void *cookie)
{
    int cpuid = smp_processor_id(), reqnum, sig;
    struct __lostagerq *rq = &lostagerq[cpuid];

    while ((reqnum = rq->out) != rq->in) {
        struct task_struct *p = rq->req[reqnum].task;
        rq->out = (reqnum + 1) & (LO_MAX_REQUESTS - 1);

        xnltt_log_event(xeno_ev_lohandler, reqnum, p->comm, p->pid);

        switch (rq->req[reqnum].type) {
            case LO_START_REQ:

#ifdef CONFIG_SMP
                if (xnshadow_thread(p))
                    /* Set up the initial task affinity using the
                       information passed to xnpod_start_thread(). */
                    set_cpus_allowed(p, xnshadow_thread(p)->affinity);
#endif /* CONFIG_SMP */

                goto do_wakeup;

            case LO_WAKEUP_REQ:

#ifdef CONFIG_SMP
                /* If the shadow thread changed its CPU while in primary mode,
                   change the CPU of its Linux counter-part (this is a cheap
                   operation, since the said Linux counter-part is suspended
                   from Linux point of view). */
                if (!xnarch_cpu_isset(cpuid, p->cpus_allowed))
                    set_cpus_allowed(p, cpumask_of_cpu(cpuid));
#endif /* CONFIG_SMP */

                /* We need to downgrade the root thread priority
                   whenever the APC runs over a non-shadow, so that
                   the temporary boost we applied in xnshadow_relax()
                   is not spuriously inherited by the latter until the
                   relaxed shadow actually resumes in secondary
                   mode. */

                if (!xnshadow_thread(current))
                    xnpod_renice_root(XNPOD_ROOT_PRIO_BASE);
              do_wakeup:

#ifdef CONFIG_XENO_OPT_ISHIELD
                if (xnshadow_thread(p) &&
                    testbits(xnshadow_thread(p)->status, XNSHIELD))
                    engage_irq_shield();
#endif /* CONFIG_XENO_OPT_ISHIELD */

                wake_up_process(p);

                if (xnsched_resched_p())
                    xnpod_schedule();

                break;

            case LO_RENICE_REQ:

                set_linux_task_priority(p, rq->req[reqnum].arg);
                break;

            case LO_SIGTHR_REQ:

                sig = rq->req[reqnum].arg;
                send_sig(sig, p, 1);
                break;

            case LO_SIGGRP_REQ:

                sig = rq->req[reqnum].arg;
                kill_proc(p->pid, sig, 1);
                break;
        }
    }
}

static void schedule_linux_call(int type, struct task_struct *p, int arg)
{
    /* Do _not_ use smp_processor_id() here so we don't trigger Linux
       preemption debug traps inadvertently (see lib/kernel_lock.c). */
    int cpuid = rthal_processor_id(), reqnum;
    struct __lostagerq *rq = &lostagerq[cpuid];
    spl_t s;

    splhigh(s);
    reqnum = rq->in;
    rq->req[reqnum].type = type;
    rq->req[reqnum].task = p;
    rq->req[reqnum].arg = arg;
    rq->in = (reqnum + 1) & (LO_MAX_REQUESTS - 1);
    splexit(s);

    rthal_apc_schedule(lostage_apc);
}

static int gatekeeper_thread(void *data)
{
    struct __gatekeeper *gk = (struct __gatekeeper *)data;
    struct task_struct *this_task = current;
    DECLARE_WAITQUEUE(wait, this_task);
    int cpu = gk - &gatekeeper[0];
    xnthread_t *thread;
    cpumask_t cpumask;
    spl_t s;

    sigfillset(&this_task->blocked);
    cpumask = cpumask_of_cpu(cpu);
    set_cpus_allowed(this_task, cpumask);
    set_linux_task_priority(this_task, MAX_RT_PRIO - 1);

    init_waitqueue_head(&gk->waitq);
    add_wait_queue_exclusive(&gk->waitq, &wait);

    up(&gk->sync);              /* Sync with xnshadow_mount(). */

    for (;;) {
        set_current_state(TASK_INTERRUPTIBLE);
        up(&gk->sync);          /* Make the request token available. */
        schedule();

        if (kthread_should_stop())
            break;

        xnlock_get_irqsave(&nklock, s);

        thread = gk->thread;

        /* In the very rare case where the requestor has been awaken
           by a signal before we have been able to process the
           pending request, just ignore the latter. */

        if (xnthread_user_task(thread)->state == TASK_INTERRUPTIBLE) {
#ifdef CONFIG_SMP
            /* If the task changed its CPU while in secondary mode,
               change the CPU of the underlying Xenomai shadow too. We
               do not migrate the thread timers here, it would not
               work. For a "full" migration comprising timers, using
               xnpod_migrate_thread is required. */
            thread->sched = xnpod_sched_slot(cpu);
#endif /* CONFIG_SMP */
            xnpod_resume_thread(thread, XNRELAX);
            xnpod_renice_root(XNPOD_ROOT_PRIO_BASE);
            xnpod_schedule();
        }

        xnlock_put_irqrestore(&nklock, s);
    }

    return 0;
}

/*! 
 * @internal
 * \fn int xnshadow_harden(void);
 * \brief Migrate a Linux task to the Xenomai domain.
 *
 * This service causes the transition of "current" from the Linux
 * domain to Xenomai. This is obtained by asking the gatekeeper to resume
 * the shadow mated with "current" then triggering the rescheduling
 * procedure in the Xenomai domain. The shadow will resume in the Xenomai
 * domain as returning from schedule().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space thread operating in secondary (i.e. relaxed) mode.
 *
 * Rescheduling: always.
 */

int xnshadow_harden(void)
{
    struct task_struct *this_task = current;
    /* Linux is not allowed to migrate shadow mates on its own, and
       shadows cannot be migrated by anyone but themselves, so the cpu
       number is constant in this context, despite the potential for
       preemption. */
    struct __gatekeeper *gk = &gatekeeper[task_cpu(this_task)];
    xnthread_t *thread = xnshadow_thread(this_task);

    if (!thread)
        return -EPERM;

    if (signal_pending(this_task) || down_interruptible(&gk->sync)) /* Grab the request token. */
        return -ERESTARTSYS;

    xnltt_log_event(xeno_ev_primarysw, this_task->comm);

    /* Set up the request to move "current" from the Linux domain to
       the Xenomai domain. This will cause the shadow thread to resume
       using the register state of the current Linux task. For this to
       happen, we set up the migration data, prepare to suspend the
       current task, wake up the gatekeeper which will perform the
       actual transition, then schedule out. Most of this sequence
       must be atomic, and we get this guarantee by disabling
       preemption and using the TASK_ATOMICSWITCH cumulative state
       provided by Adeos to Linux tasks. */

    gk->thread = thread;
    preempt_disable();
    set_current_state(TASK_INTERRUPTIBLE | TASK_ATOMICSWITCH);
    wake_up_interruptible_sync(&gk->waitq);
    schedule();

    /* Rare case: we might have been awaken by a signal before the
       gatekeeper sent us to primary mode. Since TASK_UNINTERRUPTIBLE
       is unavailable to us without wrecking the runqueue's count of
       uniniterruptible tasks, we just notice the issue and gracefully
       fail; the caller will have to process this signal anyway. */

    if (rthal_current_domain == rthal_root_domain) {
#ifdef CONFIG_XENO_OPT_DEBUG
        if (!signal_pending(this_task) || this_task->state != TASK_RUNNING)
            xnpod_fatal("xnshadow_harden() failed for thread %s[%d]",
                        thread->name, xnthread_user_pid(thread));
#endif /* CONFIG_XENO_OPT_DEBUG */
        return -ERESTARTSYS;
    }

    /* "current" is now running into the Xenomai domain. */

#ifdef CONFIG_XENO_HW_FPU
    xnpod_switch_fpu(xnpod_current_sched());
#endif /* CONFIG_XENO_HW_FPU */

    if (xnthread_signaled_p(thread))
        xnpod_dispatch_signals();

    xnlock_clear_irqon(&nklock);

    xnltt_log_event(xeno_ev_primary, thread->name);

    return 0;
}

/*! 
 * @internal
 * \fn void xnshadow_relax(int notify);
 * \brief Switch a shadow thread back to the Linux domain.
 *
 * This service yields the control of the running shadow back to
 * Linux. This is obtained by suspending the shadow and scheduling a
 * wake up call for the mated user task inside the Linux domain. The
 * Linux task will resume on return from xnpod_suspend_thread() on
 * behalf of the root thread.
 *
 * @param notify A boolean flag indicating whether threads monitored
 * from secondary mode switches should be sent a SIGXCPU signal. For
 * instance, some internal operations like task exit should not
 * trigger such signal.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space thread operating in primary (i.e. harden) mode.
 *
 * Rescheduling: always.
 *
 * @note "current" is valid here since the shadow runs with the
 * properties of the Linux task.
 */

void xnshadow_relax(int notify)
{
    xnthread_t *thread = xnpod_current_thread();
    int cprio;
    spl_t s;

#ifdef CONFIG_XENO_OPT_DEBUG
    if (testbits(thread->status, XNROOT))
        xnpod_fatal("xnshadow_relax() called from the Linux domain");
#endif /* CONFIG_XENO_OPT_DEBUG */

    /* Enqueue the request to move the running shadow from the Xenomai
       domain to the Linux domain.  This will cause the Linux task
       to resume using the register state of the shadow thread. */

    xnltt_log_event(xeno_ev_secondarysw, thread->name);

#ifdef CONFIG_XENO_OPT_ISHIELD
    if (testbits(thread->status, XNSHIELD))
        engage_irq_shield();
#endif /* CONFIG_XENO_OPT_ISHIELD */

    if (current->state & TASK_UNINTERRUPTIBLE)
        /* Just to avoid wrecking Linux's accounting of non-
           interruptible tasks, move back kicked tasks to
           interruptible state, like schedule() saw them initially. */
        set_current_state((current->
                           state & ~TASK_UNINTERRUPTIBLE) | TASK_INTERRUPTIBLE);

    schedule_linux_call(LO_WAKEUP_REQ, current, 0);

    splhigh(s);
    xnpod_renice_root(thread->cprio);
    xnpod_suspend_thread(thread, XNRELAX, XN_INFINITE, NULL);
    splexit(s);
#ifdef CONFIG_XENO_OPT_DEBUG
    if (rthal_current_domain != rthal_root_domain)
        xnpod_fatal("xnshadow_relax() failed for thread %s[%d]",
                    thread->name, xnthread_user_pid(thread));
#endif /* CONFIG_XENO_OPT_DEBUG */
    cprio = thread->cprio < MAX_RT_PRIO ? thread->cprio : MAX_RT_PRIO - 1;
    rthal_reenter_root(get_switch_lock_owner(), SCHED_FIFO, cprio ? : 1);

    xnthread_inc_ssw(thread);   /* Account for secondary mode switch. */

    if (notify && testbits(thread->status, XNTRAPSW))
        /* Help debugging spurious relaxes. */
        send_sig(SIGXCPU, current, 1);

    /* "current" is now running into the Linux domain on behalf of the
       root thread. */

    xnltt_log_event(xeno_ev_secondary, current->comm);
}

#define completion_value_ok ((1UL << (BITS_PER_LONG-1))-1)

void xnshadow_signal_completion(xncompletion_t __user * u_completion, int err)
{
    struct task_struct *p;
    pid_t pid;
    spl_t s;

    /* We should not be able to signal completion to any stale
       waiter. */

    xnlock_get_irqsave(&nklock, s);

    __xn_get_user(current, pid, &u_completion->pid);
    /* Poor man's semaphore V. */
    __xn_put_user(current, err ? : completion_value_ok,
                  &u_completion->syncflag);

    if (pid == -1) {
        /* The waiter did not enter xnshadow_wait_completion() yet:
           just raise the flag and exit. */
        xnlock_put_irqrestore(&nklock, s);
        return;
    }

    xnlock_put_irqrestore(&nklock, s);

    read_lock(&tasklist_lock);

    p = find_task_by_pid(pid);

    if (p)
        wake_up_process(p);

    read_unlock(&tasklist_lock);
}

static int xnshadow_wait_completion(xncompletion_t __user * u_completion)
{
    long syncflag;
    spl_t s;

    /* The completion block is always part of the waiter's address
       space. */

    for (;;) {                  /* Poor man's semaphore P. */
        xnlock_get_irqsave(&nklock, s);

        __xn_get_user(current, syncflag, &u_completion->syncflag);

        if (syncflag)
            break;

        __xn_put_user(current, current->pid, &u_completion->pid);

        set_current_state(TASK_INTERRUPTIBLE);

        xnlock_put_irqrestore(&nklock, s);

        schedule();

        if (signal_pending(current)) {
            __xn_put_user(current, -1, &u_completion->pid);
            syncflag = -ERESTARTSYS;
            break;
        }
    }

    xnlock_put_irqrestore(&nklock, s);

    return syncflag == completion_value_ok ? 0 : (int)syncflag;
}

void xnshadow_exit(void)
{
    rthal_reenter_root(get_switch_lock_owner(), SCHED_FIFO,
                       current->rt_priority);
    do_exit(0);
}

/*! 
 * \fn int xnshadow_map(xnthread_t *thread, xncompletion_t __user *u_completion)
 * @internal
 * \brief Create a shadow thread context.
 *
 * This call maps a nucleus thread to the "current" Linux task.  The
 * priority of the Linux task is set to the priority of the shadow
 * thread bounded to the [1..MAX_RT_PRIO-1] range, and its scheduling
 * policy is set to SCHED_FIFO. In other words, priority levels lower
 * than 1 are mapped to 1, and levels higher than MAX_RT_PRIO-1 are
 * mapped to the latter.
 *
 * @param thread The descriptor address of the new shadow thread to be
 * mapped to "current". This descriptor must have been previously
 * initialized by a call to xnpod_init_thread().
 *
 * @warning The thread must have been set the same magic number
 * (i.e. xnthread_set_magic()) than the one used to register the skin
 * it belongs to. Failing to do so might lead to unexpected results.
 *
 * @param u_completion is the address of an optional completion
 * descriptor aimed at synchronizing our parent thread with us. If
 * non-NULL, the information xnshadow_map() will store into the
 * completion block will be later used to wake up the parent thread
 * when the current shadow has been initialized. In the latter case,
 * the new shadow thread is left in a dormant state (XNDORMANT) after
 * its creation, leading to the suspension of "current" in the Linux
 * domain, only processing signals. Otherwise, the shadow thread is
 * immediately started and "current" immediately resumes in the Xenomai
 * domain from this service.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -ERESTARTSYS is returned if the current Linux task has received a
 * signal, thus preventing the final migration to the Xenomai domain
 * (i.e. in order to process the signal in the Linux domain). This
 * error should not be considered as fatal.
 *
 * - -EPERM is returned if the shadow thread has been killed before
 * the current task had a chance to return to the caller. In such a
 * case, the real-time mapping operation has failed globally, and no
 * Xenomai resource remains attached to it.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Regular user-space process. 
 *
 * Rescheduling: always.
 *
 */

int xnshadow_map(xnthread_t *thread, xncompletion_t __user * u_completion)
{
    xnarch_cpumask_t affinity;
    unsigned muxid, magic;
    int mode, prio;

    /* Increment the interface reference count. */
    magic = xnthread_get_magic(thread);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++) {
        if (muxtable[muxid].magic == magic) {
            xnarch_atomic_inc(&muxtable[muxid].refcnt);
            break;
        }
    }

    xnltt_log_event(xeno_ev_shadowmap,
                    thread->name, current->pid, xnthread_base_priority(thread));

#ifdef CONFIG_MMU
    if (!(current->mm->def_flags & VM_LOCKED))
        send_sig(SIGXCPU, current, 1);
#endif /* CONFIG_MMU */

    current->cap_effective |=
        CAP_TO_MASK(CAP_IPC_LOCK) |
        CAP_TO_MASK(CAP_SYS_RAWIO) | CAP_TO_MASK(CAP_SYS_NICE);

    xnarch_init_shadow_tcb(xnthread_archtcb(thread), thread,
                           xnthread_name(thread));
    prio =
        xnthread_base_priority(thread) <
        MAX_RT_PRIO ? xnthread_base_priority(thread) : MAX_RT_PRIO - 1;
    set_linux_task_priority(current, prio ? : 1);
    xnshadow_thrptd(current) = thread;
    xnpod_suspend_thread(thread, XNRELAX, XN_INFINITE, NULL);

    if (u_completion) {
        xnshadow_signal_completion(u_completion, 0);
        return 0;
    }

    /* Nobody waits for us, so we may start the shadow immediately
       after having forced the CPU affinity to the current
       processor. Note that we don't use smp_processor_id() to prevent
       kernel debug stuff to yell at us for calling it in a preemptible
       section of code. */

    affinity = xnarch_cpumask_of_cpu(rthal_processor_id());
    set_cpus_allowed(current, affinity);

    mode = thread->rrperiod != XN_INFINITE ? XNRRB : 0;
    xnpod_start_thread(thread, mode, 0, affinity, NULL, NULL);

    return xnshadow_harden();
}

void xnshadow_unmap(xnthread_t *thread)
{
    struct task_struct *p;
    unsigned muxid, magic;

#ifdef CONFIG_XENO_OPT_DEBUG
    if (!testbits(xnpod_current_sched()->status, XNKCOUT))
        xnpod_fatal("xnshadow_unmap() called from invalid context");
#endif /* CONFIG_XENO_OPT_DEBUG */

    p = xnthread_archtcb(thread)->user_task;    /* May be != current */

    magic = xnthread_get_magic(thread);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++) {
        if (muxtable[muxid].magic == magic) {
            if (xnarch_atomic_dec_and_test(&muxtable[muxid].refcnt))
                /* We were the last thread, decrement the counter,
                   since it was incremented by the xn_sys_bind
                   operation. */
                xnarch_atomic_dec(&muxtable[muxid].refcnt);

            break;
        }
    }

    xnltt_log_event(xeno_ev_shadowunmap, thread->name, p ? p->pid : -1);
    if (!p)
        goto renice_and_exit;

    xnshadow_thrptd(p) = NULL;

    if (p->state != TASK_RUNNING) {
        /* If the shadow is being unmapped in primary mode or blocked
           in secondary mode, the associated Linux task should also
           die. In the former case, the zombie Linux side returning to
           user-space will be trapped and exited inside the pod's
           rescheduling routines. */
        schedule_linux_call(LO_WAKEUP_REQ, p, 0);
        return;
    }

  renice_and_exit:
    /* Otherwise, if the shadow is being unmapped in secondary mode
       and running, we only detach the shadow thread from its Linux
       mate, and renice the root thread appropriately. We do not
       reschedule since xnshadow_unmap() must be called from a thread
       deletion hook. */
    xnpod_renice_root(XNPOD_ROOT_PRIO_BASE);
}

int xnshadow_wait_barrier(struct pt_regs *regs)
{
    xnthread_t *thread = xnshadow_thread(current);
    spl_t s;

    if (!thread)
        return -EPERM;

    xnlock_get_irqsave(&nklock, s);

    if (testbits(thread->status, XNSTARTED)) {
        /* Already done -- no op. */
        xnlock_put_irqrestore(&nklock, s);
        goto release_task;
    }

    /* We must enter this call on behalf of the Linux domain. */
    set_current_state(TASK_INTERRUPTIBLE);
    xnlock_put_irqrestore(&nklock, s);

    schedule();

    if (signal_pending(current))
        return -ERESTARTSYS;

    if (!testbits(thread->status, XNSTARTED))   /* Paranoid. */
        return -EPERM;

  release_task:

    if (__xn_reg_arg1(regs))
        __xn_copy_to_user(current,
                          (void __user *)__xn_reg_arg1(regs),
                          &thread->entry, sizeof(thread->entry));

    if (__xn_reg_arg2(regs))
        __xn_copy_to_user(current,
                          (void __user *)__xn_reg_arg2(regs),
                          &thread->cookie, sizeof(thread->cookie));

    return xnshadow_harden();
}

void xnshadow_start(xnthread_t *thread)
{
    struct task_struct *p;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    xnpod_resume_thread(thread, XNDORMANT);
    p = xnthread_archtcb(thread)->user_task;
    xnltt_log_event(xeno_ev_shadowstart, thread->name);

    xnlock_put_irqrestore(&nklock, s);

    if (p->state == TASK_INTERRUPTIBLE)
        /* Wakeup the Linux mate waiting on the barrier. */
        schedule_linux_call(LO_START_REQ, p, 0);
}

void xnshadow_renice(xnthread_t *thread)
{
    /* Called with nklock locked, Xenomai interrupts off. */
    struct task_struct *p = xnthread_archtcb(thread)->user_task;
    /* We need to bound the priority values in the [1..MAX_RT_PRIO-1]
       range, since the core pod's priority scale is a superset of
       Linux's priority scale. */
    int prio = thread->cprio < MAX_RT_PRIO ? thread->cprio : MAX_RT_PRIO - 1;
    schedule_linux_call(LO_RENICE_REQ, p, prio ? : 1);
}

void xnshadow_suspend(xnthread_t *thread)
{
    /* Called with nklock locked, Xenomai interrupts off. */
    struct task_struct *p = xnthread_archtcb(thread)->user_task;
    schedule_linux_call(LO_SIGTHR_REQ, p, SIGCHLD);
}

static void stringify_feature_set(u_long fset, char *buf, int size)
{
    unsigned long feature;
    int nc, nfeat;

    *buf = '\0';

    for (feature = 1, nc = nfeat = 0; fset != 0 && size > 0; feature <<= 1) {
        if (fset & feature) {
            nc = snprintf(buf, size, "%s%s",
                          nfeat > 0 ? " " : "", get_feature_label(feature));
            nfeat++;
            size -= nc;
            buf += nc;
            fset &= ~feature;
        }
    }
}

static int bind_to_interface(struct task_struct *curr,
                             unsigned magic,
                             u_long featdep, u_long abirev, u_long infarg)
{
    xnshadow_ppd_t *ppd = NULL;
    xnfeatinfo_t finfo;
    u_long featmis;
    int muxid;
    spl_t s;

    featmis = (~XENOMAI_FEAT_DEP & (featdep & XENOMAI_FEAT_MAN));

    if (infarg) {
        if (!__xn_access_ok(curr, VERIFY_WRITE, infarg, sizeof(finfo)))
            return -EFAULT;

        /* Pass back the supported feature set and the ABI revision
           level to user-space. */

        finfo.feat_all = XENOMAI_FEAT_DEP;
        stringify_feature_set(XENOMAI_FEAT_DEP, finfo.feat_all_s,
                              sizeof(finfo.feat_all_s));
        finfo.feat_man = featdep & XENOMAI_FEAT_MAN;
        stringify_feature_set(XENOMAI_FEAT_MAN, finfo.feat_man_s,
                              sizeof(finfo.feat_man_s));
        finfo.feat_mis = featmis;
        stringify_feature_set(featmis, finfo.feat_mis_s,
                              sizeof(finfo.feat_mis_s));
        finfo.feat_req = featdep;
        stringify_feature_set(featdep, finfo.feat_req_s,
                              sizeof(finfo.feat_req_s));
        finfo.abirev = XENOMAI_ABI_REV;

        __xn_copy_to_user(curr, (void *)infarg, &finfo, sizeof(finfo));
    }

    if (featmis)
        /* Some mandatory features the user-space interface relies on
           are missing at kernel level; cannot go further. */
        return -EINVAL;

    if (!check_abi_revision(abirev))
        return -ENOEXEC;

    xnlock_get_irqsave(&nklock, s);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
        if (muxtable[muxid].magic == magic)
            goto do_bind;

    xnlock_put_irqrestore(&nklock, s);

    return -ESRCH;

  do_bind:

    /* Increment the reference count now (actually, only the first
       call to bind_to_interface() really increments the counter), so
       that the interface cannot be removed under our feet. */

    if (!xnarch_atomic_inc_and_test(&muxtable[muxid].refcnt))
        xnarch_atomic_dec(&muxtable[muxid].refcnt);

    xnlock_put_irqrestore(&nklock, s);

    /* Since the pod might be created by the event callback and not
       earlier than that, do not refer to nkpod until the latter had a
       chance to call xnpod_init(). */

    if (muxtable[muxid].eventcb) {
        xnlock_get_irqsave(&nklock, s);

        ppd = xnshadow_ppd_lookup(muxid, curr->mm);

        /* protect from the same process binding several time. */
        if (!ppd) {
            ppd = (xnshadow_ppd_t *)
                muxtable[muxid].eventcb(XNSHADOW_CLIENT_ATTACH, curr);

            if (IS_ERR(ppd)) {
                xnarch_atomic_dec(&muxtable[muxid].refcnt);
                xnlock_put_irqrestore(&nklock, s);
                return PTR_ERR(ppd);
            }

            if (ppd) {
                ppd->key.muxid = muxid;
                ppd->key.mm = curr->mm;
                xnshadow_ppd_insert(ppd);
            }
        }

        xnlock_put_irqrestore(&nklock, s);
    }

    if (!nkpod || testbits(nkpod->status, XNPIDLE)) {
        /* Ok mate, but you really ought to create some pod in a way
           or another if you want me to be of some help here... */
        if (muxtable[muxid].eventcb && ppd) {
            xnshadow_ppd_remove(ppd);
            muxtable[muxid].eventcb(XNSHADOW_CLIENT_DETACH, ppd);
        }

        xnarch_atomic_dec(&muxtable[muxid].refcnt);
        return -ENOSYS;
    }

    return ++muxid;
}

static int get_system_info(struct task_struct *curr, int muxid, /* unused */
                           u_long infarg)
{
    xnsysinfo_t info;

    if (!__xn_access_ok(curr, VERIFY_WRITE, infarg, sizeof(info)))
        return -EFAULT;

    info.cpufreq = xnarch_get_cpu_freq();
    info.tickval = xnpod_get_tickval();
    __xn_copy_to_user(curr, (void *)infarg, &info, sizeof(info));

    return 0;
}

static inline int substitute_linux_syscall(struct task_struct *curr,
                                           struct pt_regs *regs)
{
    /* No real-time replacement for now -- let Linux handle this call. */
    return 0;
}

static void exec_nucleus_syscall(int muxop, struct pt_regs *regs)
{
    int err;

    /* Called on behalf of the root thread. */

    switch (muxop) {
        case __xn_sys_completion:

            __xn_status_return(regs,
                               xnshadow_wait_completion((xncompletion_t __user
                                                         *)
                                                        __xn_reg_arg1(regs)));
            break;

        case __xn_sys_migrate:

            if ((err = xnshadow_harden()) != 0)
                __xn_error_return(regs, err);
            else
                __xn_success_return(regs, 1);

            break;

        case __xn_sys_barrier:

            __xn_status_return(regs, xnshadow_wait_barrier(regs));
            break;

        case __xn_sys_bind:

            __xn_status_return(regs,
                               bind_to_interface(current,
                                                 __xn_reg_arg1(regs),
                                                 __xn_reg_arg2(regs),
                                                 __xn_reg_arg3(regs),
                                                 __xn_reg_arg4(regs)));
            break;

        case __xn_sys_info:

            __xn_status_return(regs,
                               get_system_info(current,
                                               __xn_reg_arg1(regs),
                                               __xn_reg_arg2(regs)));
            break;

        case __xn_sys_arch:

            /* A special syscall channel, available for implementing
               arch-dependent system calls (see asm-<arch>/system.h
               for local implementations). */

            __xn_status_return(regs, xnarch_local_syscall(regs));
            break;

        default:

            printk(KERN_WARNING "Xenomai: Unknown nucleus syscall #%d\n",
                   muxop);
    }
}

void xnshadow_send_sig(xnthread_t *thread, int sig, int specific)
{
    schedule_linux_call(specific ? LO_SIGTHR_REQ : LO_SIGGRP_REQ,
                        xnthread_user_task(thread), sig);
}

static inline int do_hisyscall_event(unsigned event, unsigned domid, void *data)
{
    struct pt_regs *regs = (struct pt_regs *)data;
    int muxid, muxop, switched, err;
    struct task_struct *p;
    xnthread_t *thread;
    u_long sysflags;

    if (!nkpod || testbits(nkpod->status, XNPIDLE))
        goto no_skin;

    if (xnsched_resched_p())
        xnpod_schedule();

    p = current;
    thread = xnshadow_thread(p);

    if (!__xn_reg_mux_p(regs))
        goto linux_syscall;

#ifdef CONFIG_XENO_OPT_SECURITY_ACCESS
    if (unlikely(!cap_raised(p->cap_effective, CAP_SYS_NICE))) {
        __xn_error_return(regs, -EPERM);
        return RTHAL_EVENT_STOP;
    }
#endif /* CONFIG_XENO_OPT_SECURITY_ACCESS */

    muxid = __xn_mux_id(regs);
    muxop = __xn_mux_op(regs);

    xnltt_log_event(xeno_ev_syscall, thread->name, muxid, muxop);

    if (muxid != 0)
        goto skin_syscall;

    /* Nucleus internal syscall. */

    switch (muxop) {
        case __xn_sys_migrate:

            if (__xn_reg_arg1(regs) == XENOMAI_XENO_DOMAIN) {   /* Linux => Xenomai */
                if (!thread)    /* Not a shadow -- cannot migrate to Xenomai. */
                    __xn_error_return(regs, -EPERM);
                else if (!xnthread_test_flags(thread, XNRELAX))
                    __xn_success_return(regs, 0);
                else
                    /* Migration to Xenomai from the Linux domain
                       must be done from the latter: propagate
                       the request to the Linux-level handler. */
                    return RTHAL_EVENT_PROPAGATE;
            } else if (__xn_reg_arg1(regs) == XENOMAI_LINUX_DOMAIN) {   /* Xenomai => Linux */
                if (!thread || xnthread_test_flags(thread, XNRELAX))
                    __xn_success_return(regs, 0);
                else {
                    __xn_success_return(regs, 1);
                    xnshadow_relax(0);  /* Don't notify upon explicit migration. */
                }
            } else
                __xn_error_return(regs, -EINVAL);

            goto nucleus_syscall_done;

        case __xn_sys_arch:

            /* We don't want to switch mode here. */
            __xn_status_return(regs, xnarch_local_syscall(regs));
            goto nucleus_syscall_done;

        case __xn_sys_bind:
        case __xn_sys_info:
        case __xn_sys_completion:
        case __xn_sys_barrier:

            /* If called from Xenomai, switch to secondary mode then run
             * the internal syscall afterwards. If called from Linux,
             * propagate the event so that linux_sysentry() will catch
             * it and run the syscall from there. We need to play this
             * trick here and at a few other locations because Adeos
             * will propagate events down the pipeline up to (and
             * including) the calling domain itself, so if Xenomai is
             * the original caller, there is no way Linux can receive
             * the syscall from propagation because Adeos won't cross
             * the boundary delimited by the calling Xenomai stage for
             * this particular syscall instance. If the latter is
             * still unclear in your mind, have a look at the
             * ipipe/adeos_catch_event() documentation and get back to
             * this later. */

            if (domid == RTHAL_DOMAIN_ID) {
                xnshadow_relax(1);
                exec_nucleus_syscall(muxop, regs);

              nucleus_syscall_done:
                if (xnpod_shadow_p() && signal_pending(p))
                    request_syscall_restart(thread, regs);

                return RTHAL_EVENT_STOP;
            }

            /* Delegate the syscall handling to the Linux domain. */
            return RTHAL_EVENT_PROPAGATE;

        default:

          bad_syscall:
            __xn_error_return(regs, -ENOSYS);
            return RTHAL_EVENT_STOP;
    }

  skin_syscall:

    if (muxid < 0 || muxid > XENOMAI_MUX_NR ||
        muxop < 0 || muxop >= muxtable[muxid - 1].nrcalls)
        goto bad_syscall;

    sysflags = muxtable[muxid - 1].systab[muxop].flags;

    if ((sysflags & __xn_exec_shadow) != 0 && !thread) {
        __xn_error_return(regs, -EPERM);
        return RTHAL_EVENT_STOP;
    }

    if ((sysflags & __xn_exec_conforming) != 0)
        /* If the conforming exec bit has been set, turn the exec
           bitmask for the syscall into the most appropriate setup for
           the caller, i.e. Xenomai domain for shadow threads, Linux
           otherwise. */
        sysflags |= (thread ? __xn_exec_histage : __xn_exec_lostage);

    /*
     * Here we have to dispatch the syscall execution properly,
     * depending on:
     *
     * o Whether the syscall must be run into the Linux or Xenomai
     * domain, or indifferently in the current Xenomai domain.
     *
     * o Whether the caller currently runs in the Linux or Xenomai
     * domain.
     */

    switched = 0;

  restart:                     /* Process adaptive syscalls by restarting them in the
                                   opposite domain. */

    if ((sysflags & __xn_exec_lostage) != 0) {
        /* Syscall must run into the Linux domain. */

        if (domid == RTHAL_DOMAIN_ID) {
            /* Request originates from the Xenomai domain: just relax the
               caller and execute the syscall immediately after. */
            xnshadow_relax(1);
            switched = 1;
        } else
            /* Request originates from the Linux domain: propagate the
               event to our Linux-based handler, so that the syscall
               is executed from there. */
            goto propagate_syscall;
    } else if ((sysflags & (__xn_exec_histage | __xn_exec_current)) != 0) {
        /* Syscall must be processed either by Xenomai, or by the
           calling domain. */

        if (domid != RTHAL_DOMAIN_ID)
            /* Request originates from the Linux domain: propagate the
               event to our Linux-based handler, so that the caller is
               hardened and the syscall is eventually executed from
               there. */
            goto propagate_syscall;

        /* Request originates from the Xenomai domain: run the syscall
           immediately. */
    }

    err = muxtable[muxid - 1].systab[muxop].svc(p, regs);

    if (err == -ENOSYS && (sysflags & __xn_exec_adaptive) != 0) {
        if (switched) {
            switched = 0;

            if ((err = xnshadow_harden()) != 0)
                goto done;
        }

        sysflags ^=
            (__xn_exec_lostage | __xn_exec_histage | __xn_exec_adaptive);
        goto restart;
    }

  done:

    __xn_status_return(regs, err);

    if (xnpod_shadow_p() && signal_pending(p))
        request_syscall_restart(thread, regs);
    else if ((sysflags & __xn_exec_switchback) != 0 && switched)
        xnshadow_harden();      /* -EPERM will be trapped later if needed. */

    return RTHAL_EVENT_STOP;

  linux_syscall:

    if (xnpod_root_p())
        /* The call originates from the Linux domain, either from a
           relaxed shadow or from a regular Linux task; just propagate
           the event so that we will fall back to linux_sysentry(). */
        goto propagate_syscall;

    /* From now on, we know that we have a valid shadow thread
       pointer. */

    if (substitute_linux_syscall(p, regs))
        /* This is a Linux syscall issued on behalf of a shadow thread
           running inside the Xenomai domain. This call has just been
           intercepted by the nucleus and a Xenomai replacement has been
           substituted for it. */
        return RTHAL_EVENT_STOP;

    /* This syscall has not been substituted, let Linux handle
       it. This will eventually fall back to the Linux syscall handler
       if our Linux domain handler does not intercept it. Before we
       let it go, ensure that our running thread has properly entered
       the Linux domain. */

    xnshadow_relax(1);

    goto propagate_syscall;

  no_skin:

    if (__xn_reg_mux_p(regs)) {
        if (__xn_reg_mux(regs) == __xn_mux_code(0, __xn_sys_bind))
            /* Valid exception case: we may be called to bind to a
               skin which will create its own pod through its callback
               routine before returning to user-space. */
            goto propagate_syscall;

        xnlogwarn("bad syscall %ld/%ld -- no skin loaded.\n",
                  __xn_mux_id(regs), __xn_mux_op(regs));

        __xn_error_return(regs, -ENOSYS);
        return RTHAL_EVENT_STOP;
    }

    /* Regular Linux syscall with no skin loaded -- propagate it
       to the Linux kernel. */

  propagate_syscall:

    return RTHAL_EVENT_PROPAGATE;
}

RTHAL_DECLARE_EVENT(hisyscall_event);

static inline int do_losyscall_event(unsigned event, unsigned domid, void *data)
{
    struct pt_regs *regs = (struct pt_regs *)data;
    xnthread_t *thread = xnshadow_thread(current);
    int muxid, muxop, sysflags, switched, err;

    if (__xn_reg_mux_p(regs))
        goto xenomai_syscall;

    if (!thread || !substitute_linux_syscall(current, regs))
        /* Fall back to Linux syscall handling. */
        return RTHAL_EVENT_PROPAGATE;

    /* This is a Linux syscall issued on behalf of a shadow thread
       running inside the Linux domain. If the call has been
       substituted with a Xenomai replacement, do not let Linux know
       about it. */

    return RTHAL_EVENT_STOP;

  xenomai_syscall:

    /* muxid and muxop have already been checked in the Xenomai domain
       handler. */

    muxid = __xn_mux_id(regs);
    muxop = __xn_mux_op(regs);

    xnltt_log_event(xeno_ev_syscall,
                    nkpod ? xnpod_current_thread()->name : "<system>",
                    muxid, muxop);

    if (muxid == 0) {
        /* These are special built-in services which must run on
           behalf of the Linux domain (over which we are currently
           running). */
        exec_nucleus_syscall(muxop, regs);

        if (nkpod && xnpod_shadow_p() && signal_pending(current))
            request_syscall_restart(thread, regs);

        return RTHAL_EVENT_STOP;
    }

    /* Processing a real-time skin syscall. */

    sysflags = muxtable[muxid - 1].systab[muxop].flags;

    if ((sysflags & __xn_exec_conforming) != 0)
        sysflags |= (thread ? __xn_exec_histage : __xn_exec_lostage);

  restart:                     /* Process adaptive syscalls by restarting them in the
                                   opposite domain. */

    if ((sysflags & __xn_exec_histage) != 0) {
        /* This request originates from the Linux domain and must be
           run into the Xenomai domain: harden the caller and execute the
           syscall. */
        if ((err = xnshadow_harden()) != 0) {
            __xn_error_return(regs, err);
            return RTHAL_EVENT_STOP;
        }

        switched = 1;
    } else                      /* We want to run the syscall in the Linux domain.  */
        switched = 0;

    err = muxtable[muxid - 1].systab[muxop].svc(current, regs);

    if (err == -ENOSYS && (sysflags & __xn_exec_adaptive) != 0) {
        if (switched) {
            switched = 0;
            xnshadow_relax(1);
        }

        sysflags ^=
            (__xn_exec_lostage | __xn_exec_histage | __xn_exec_adaptive);
        goto restart;
    }

    __xn_status_return(regs, err);

    if (xnpod_shadow_p() && signal_pending(current))
        request_syscall_restart(xnshadow_thread(current), regs);
    else if ((sysflags & __xn_exec_switchback) != 0 && switched)
        xnshadow_relax(0);

    return RTHAL_EVENT_STOP;
}

RTHAL_DECLARE_EVENT(losyscall_event);

static inline void do_taskexit_event(struct task_struct *p)
{
    xnthread_t *thread = xnshadow_thread(p);    /* p == current */

    if (!thread)
        return;

    if (xnpod_shadow_p())
        xnshadow_relax(0);

    /* So that we won't attempt to further wakeup the exiting task in
       xnshadow_unmap(). */

    xnshadow_thrptd(p) = NULL;
    xnthread_archtcb(thread)->user_task = NULL;
    xnpod_delete_thread(thread);    /* Should indirectly call xnshadow_unmap(). */

    xnltt_log_event(xeno_ev_shadowexit, thread->name);
}

RTHAL_DECLARE_EXIT_EVENT(taskexit_event);

static inline void do_schedule_event(struct task_struct *next)
{
    struct task_struct *prev;
    int oldrprio, newrprio;
    xnthread_t *threadin;
    rthal_declare_cpuid;

    if (!nkpod || testbits(nkpod->status, XNPIDLE))
        return;

    prev = current;
    threadin = xnshadow_thread(next);

    rthal_load_cpuid();         /* Linux is running in a migration-safe
                                   portion of code. */

    set_switch_lock_owner(prev);

    if (threadin) {
        /* Check whether we need to unlock the timers, each time a
           Linux task resumes from a stopped state, excluding tasks
           resuming shortly for entering a stopped state asap due to
           ptracing. To identify the latter, we need to check for
           SIGSTOP and SIGINT in order to encompass both the NPTL and
           LinuxThreads behaviours. */

        if (testbits(threadin->status, XNDEBUG)) {
            if (signal_pending(next)) {
                sigset_t pending;

                spin_lock(&wrap_sighand_lock(next));    /* Already interrupt-safe. */
                wrap_get_sigpending(&pending, next);
                spin_unlock(&wrap_sighand_lock(next));

                if (sigismember(&pending, SIGSTOP) ||
                    sigismember(&pending, SIGINT))
                    goto no_ptrace;
            }

            clrbits(threadin->status, XNDEBUG);
            unlock_timers();
        }

      no_ptrace:

        newrprio = threadin->cprio;

#ifdef CONFIG_XENO_OPT_DEBUG
        {
            xnflags_t status = threadin->status;
            int sigpending = signal_pending(next);

            if (!testbits(status, XNRELAX)) {
                show_stack(xnthread_user_task(threadin), NULL);
                xnpod_fatal
                    ("Hardened thread %s[%d] running in Linux domain?! (status=0x%lx, sig=%d, prev=%s[%d])",
                     threadin->name, next->pid, status, sigpending, prev->comm,
                     prev->pid);
            } else if (!(next->ptrace & PT_PTRACED) &&
                       /* Allow ptraced threads to run shortly in order to
                          properly recover from a stopped state. */
                       testbits(status, XNSTARTED) && testbits(status, XNPEND)) {
                ipipe_trace_panic_freeze();
                show_stack(xnthread_user_task(threadin), NULL);
                xnpod_fatal
                    ("blocked thread %s[%d] rescheduled?! (status=0x%lx, sig=%d, prev=%s[%d])",
                     threadin->name, next->pid, status, sigpending, prev->comm,
                     prev->pid);
            }
        }
#endif /* CONFIG_XENO_OPT_DEBUG */

#ifdef CONFIG_XENO_OPT_ISHIELD
        reset_shield(threadin);
#endif /* CONFIG_XENO_OPT_ISHIELD */
    } else if (next != gatekeeper[cpuid].server) {
        newrprio = XNPOD_ROOT_PRIO_BASE;
#ifdef CONFIG_XENO_OPT_ISHIELD
        disengage_irq_shield();
#endif /* CONFIG_XENO_OPT_ISHIELD */
    } else
        return;

    /* Current nucleus thread must be the root one in this context, so
       we can safely renice the nucleus's runthread (i.e. as returned
       by xnpod_current_thread()). */

    oldrprio = xnpod_current_thread()->cprio;

    if (oldrprio != newrprio) {
        xnpod_renice_root(newrprio);

        if (xnpod_compare_prio(newrprio, oldrprio) < 0)
            /* Subtle: by downgrading the root thread priority, some
               higher priority thread might well become eligible for
               execution instead of us. Since xnpod_renice_root() does
               not reschedule (and must _not_ in most of other cases),
               let's call the rescheduling procedure ourselves. */
            xnpod_schedule();
    }
}

RTHAL_DECLARE_SCHEDULE_EVENT(schedule_event);

static inline void do_sigwake_event(struct task_struct *p)
{
    xnthread_t *thread = xnshadow_thread(p);
    spl_t s;

    if (!thread || testbits(thread->status, XNROOT))    /* Eh? root as shadow? */
        return;

    xnlock_get_irqsave(&nklock, s);

    if ((p->ptrace & PT_PTRACED) && !testbits(thread->status, XNDEBUG)) {
        sigset_t pending;

        /* We already own the siglock. */
        wrap_get_sigpending(&pending, p);

        if (sigismember(&pending, SIGTRAP) ||
            sigismember(&pending, SIGSTOP) || sigismember(&pending, SIGINT)) {
            __setbits(thread->status, XNDEBUG);
            lock_timers();
        }
    }

    if (testbits(thread->status, XNRELAX))
        goto unlock_and_exit;

    if (thread == thread->sched->runthread)
        xnsched_set_resched(thread->sched);

    if (xnpod_unblock_thread(thread))
        __setbits(thread->status, XNKICKED);

    if (testbits(thread->status, XNSUSP)) {
        xnpod_resume_thread(thread, XNSUSP);
        __setbits(thread->status, XNKICKED);
    }

    /* If we are kicking a shadow thread, make sure Linux won't
       schedule in its mate under our feet as a result of running
       signal_wake_up(). The Xenomai scheduler must remain in control for
       now, until we explicitely relax the shadow thread to allow for
       processing the pending signals. Make sure we keep the
       additional state flags unmodified so that we don't break any
       undergoing ptrace. */

    if (p->state & TASK_INTERRUPTIBLE)
        set_task_state(p,
                       (p->state & ~TASK_INTERRUPTIBLE) | TASK_UNINTERRUPTIBLE);

    xnpod_schedule();

  unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);
}

RTHAL_DECLARE_SIGWAKE_EVENT(sigwake_event);

static inline void do_setsched_event(struct task_struct *p, int priority)
{
    xnthread_t *thread = xnshadow_thread(p);

    if (!thread)
        return;

    /* Linux's priority scale is a subset of the core pod's priority
       scale, so there is no need to bound the priority values when
       mapping them from Linux -> Xenomai. */

    if (thread->cprio != priority)
        xnpod_renice_thread_inner(thread, priority, 0);

    if (current == p && thread->cprio != xnpod_current_root()->cprio)
        xnpod_renice_root(thread->cprio);

    if (xnsched_resched_p())
        xnpod_schedule();
}

RTHAL_DECLARE_SETSCHED_EVENT(setsched_event);

static void detach_ppd(xnshadow_ppd_t *ppd)
{
    muxtable[xnshadow_ppd_muxid(ppd)].eventcb(XNSHADOW_CLIENT_DETACH, ppd);
}

static inline void do_cleanup_event (struct mm_struct *mm)
{
    xnshadow_ppd_remove_mm(mm, &detach_ppd);
}

RTHAL_DECLARE_CLEANUP_EVENT(cleanup_event);

/*
 * xnshadow_register_interface() -- Register a new skin/interface.
 * NOTE: an interface can be registered without its pod being
 * necessarily active. In such a case, a lazy initialization scheme
 * can be implemented through the event callback fired upon the first
 * client binding.
 *
 * The event callback will be called with its first argument set to:
 * - XNSHADOW_CLIENT_ATTACH, when a user-space process binds the interface, the
 *   second argument being the task_struct pointer of the calling thread, the
 *   callback may then return:
 *   . a pointer to an xnshadow_ppd_t structure, meaning that this structure
 *   will be attached to the calling process for this interface;
 *   . a NULL pointer, meaning that no per-process structure should be attached
 *   to this process for this interface;
 *   . ERR_PTR(negative value) indicating an error, the binding process will
 *   then abort;
 * - XNSHADOW_DETACH, when a user-space process terminates, if a non-NULL
 *   per-process structure is attached to the dying process, the second argument
 *   being the pointer to the per-process data attached to the dying process.
 */

int xnshadow_register_interface(const char *name,
                                unsigned magic,
                                int nrcalls,
                                xnsysent_t *systab,
                                void *(*eventcb)(int, void *))
{
    int muxid;
    spl_t s;

    /* We can only handle up to 256 syscalls per skin, check for over-
       and underflow (MKL). */

    if (XENOMAI_MAX_SYSENT < nrcalls || 0 > nrcalls)
        return -EINVAL;

    xnlock_get_irqsave(&nklock, s);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++) {
        if (muxtable[muxid].systab == NULL) {
            muxtable[muxid].name = name;
            muxtable[muxid].systab = systab;
            muxtable[muxid].nrcalls = nrcalls;
            muxtable[muxid].magic = magic;
            xnarch_atomic_set(&muxtable[muxid].refcnt, -1);
            muxtable[muxid].eventcb = eventcb;

            xnlock_put_irqrestore(&nklock, s);

#ifdef CONFIG_PROC_FS
            xnpod_declare_iface_proc(muxtable + muxid);
#endif /* CONFIG_PROC_FS */

            return muxid + 1;
        }
    }

    xnlock_put_irqrestore(&nklock, s);

    return -ENOBUFS;
}

/*
 * xnshadow_unregister_interface() -- Unregister a new skin/interface.
 * NOTE: an interface can be unregistered without its pod being
 * necessarily active.
 */

int xnshadow_unregister_interface(int muxid)
{
    int err = 0;
    spl_t s;

    if (--muxid < 0 || muxid >= XENOMAI_MUX_NR)
        return -EINVAL;

    xnlock_get_irqsave(&nklock, s);

    if (xnarch_atomic_get(&muxtable[muxid].refcnt) <= 0) {
        muxtable[muxid].systab = NULL;
        muxtable[muxid].nrcalls = 0;
        muxtable[muxid].magic = 0;
        xnarch_atomic_set(&muxtable[muxid].refcnt, -1);
#ifdef CONFIG_PROC_FS
        xnpod_discard_iface_proc(muxtable + muxid);
#endif /* CONFIG_PROC_FS */
    } else
        err = -EBUSY;

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

/**
 * Return the per-process data attached to the calling process.
 *
 * This service returns the per-process data attached to the calling process for
 * the skin whose muxid is @a muxid. It must be called with nklock locked, irqs
 * off.
 *
 * See xnshadow_register_interface() documentation for information on the way to
 * attach a per-process data to a process.
 *
 * @param muxid the skin muxid.
 *
 * @return the per-process data if the current context is a user-space process;
 * @return NULL otherwise.
 * 
 */
xnshadow_ppd_t *xnshadow_ppd_get(unsigned muxid)
{
    if (xnpod_userspace_p())
        return xnshadow_ppd_lookup(muxid - 1, current->mm);

    return NULL;
}

void xnshadow_grab_events(void)
{
    rthal_catch_taskexit(&taskexit_event);
    rthal_catch_sigwake(&sigwake_event);
    rthal_catch_schedule(&schedule_event);
    rthal_catch_setsched(&setsched_event);
    rthal_catch_cleanup(&cleanup_event);
}

void xnshadow_release_events(void)
{
    rthal_catch_taskexit(NULL);
    rthal_catch_sigwake(NULL);
    rthal_catch_schedule(NULL);
    rthal_catch_setsched(NULL);
    rthal_catch_cleanup(NULL);
}

int xnshadow_mount(void)
{
    unsigned i, size;
    int cpu;

#ifdef CONFIG_XENO_OPT_ISHIELD
    if (rthal_register_domain(&irq_shield,
                              "IShield",
                              0x53484c44,
                              RTHAL_ROOT_PRIO + 50, &shield_domain_entry))
        return -EBUSY;

    shielded_cpus = CPU_MASK_NONE;
    unshielded_cpus = xnarch_cpu_online_map;
#endif /* CONFIG_XENO_OPT_ISHIELD */

    nkthrptd = rthal_alloc_ptdkey();
    nkerrptd = rthal_alloc_ptdkey();

    if (nkthrptd < 0 || nkerrptd < 0) {
        printk(KERN_WARNING "Xenomai: cannot allocate PTD slots\n");
        return -ENOMEM;
    }

    lostage_apc = rthal_apc_alloc("lostage_handler", &lostage_handler, NULL);

    for_each_online_cpu(cpu) {
        struct __gatekeeper *gk = &gatekeeper[cpu];
        sema_init(&gk->sync, 0);
        xnarch_memory_barrier();
        gk->server =
            kthread_create(&gatekeeper_thread, gk, "gatekeeper/%d", cpu);
        wake_up_process(gk->server);
        down(&gk->sync);
    }

    /* We need to grab these ones right now. */
    rthal_catch_losyscall(&losyscall_event);
    rthal_catch_hisyscall(&hisyscall_event);

    size = sizeof(xnqueue_t) * XNSHADOW_PPD_HASH_SIZE;
    xnshadow_ppd_hash = (xnqueue_t *) xnarch_sysalloc(size);
    if (!xnshadow_ppd_hash)
        {
        xnshadow_cleanup();
        printk(KERN_WARNING "Xenomai: cannot allocate PPD hash table.\n");
        return -ENOMEM;
        }

    for (i = 0; i < XNSHADOW_PPD_HASH_SIZE; i++)
        initq(&xnshadow_ppd_hash[i]);
    return 0;
}

void xnshadow_cleanup(void)
{
    int cpu;

    if (xnshadow_ppd_hash)
        xnarch_sysfree(xnshadow_ppd_hash,
                       sizeof(xnqueue_t) * XNSHADOW_PPD_HASH_SIZE);

    rthal_catch_losyscall(NULL);
    rthal_catch_hisyscall(NULL);

    for_each_online_cpu(cpu) {
        struct __gatekeeper *gk = &gatekeeper[cpu];
        down(&gk->sync);
        gk->thread = NULL;
        kthread_stop(gk->server);
    }

    rthal_apc_free(lostage_apc);
    rthal_free_ptdkey(nkerrptd);
    rthal_free_ptdkey(nkthrptd);
#ifdef CONFIG_XENO_OPT_ISHIELD
    rthal_unregister_domain(&irq_shield);
#endif /* CONFIG_XENO_OPT_ISHIELD */
}

/*@}*/

EXPORT_SYMBOL(xnshadow_map);
EXPORT_SYMBOL(xnshadow_register_interface);
EXPORT_SYMBOL(xnshadow_harden);
EXPORT_SYMBOL(xnshadow_relax);
EXPORT_SYMBOL(xnshadow_start);
EXPORT_SYMBOL(xnshadow_signal_completion);
EXPORT_SYMBOL(xnshadow_unmap);
EXPORT_SYMBOL(xnshadow_send_sig);
EXPORT_SYMBOL(xnshadow_unregister_interface);
EXPORT_SYMBOL(xnshadow_wait_barrier);
EXPORT_SYMBOL(xnshadow_suspend);
EXPORT_SYMBOL(xnshadow_ppd_get);
EXPORT_SYMBOL(nkthrptd);
EXPORT_SYMBOL(nkerrptd);
