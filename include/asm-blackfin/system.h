/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _XENO_ASM_BFINNOMMU_SYSTEM_H
#define _XENO_ASM_BFINNOMMU_SYSTEM_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/ptrace.h>
#include <asm-generic/xenomai/system.h>
#include <asm/system.h>
#include <asm/processor.h>

#define XNARCH_DEFAULT_TICK     1000000 /* ns, i.e. 1ms */
/* The I-pipe frees the Blackfin core timer for us, therefore we don't
   need any host tick relay service since the regular Linux time
   source is still ticking in parallel at the normal pace through
   TIMER0. */
#define XNARCH_HOST_TICK        0
#define XNARCH_THREAD_STACKSZ   8192

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)

struct xnthread;
struct task_struct;

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    /* Kernel mode side */

#define xnarch_fpu_ptr(tcb)     NULL /* No FPU handling at all. */

    unsigned stacksize;		/* Aligned size of stack (bytes) */
    unsigned long *stackbase;	/* Stack space */

    struct thread_struct ts;	/* Holds kernel-based thread context. */
    struct task_struct *user_task; /* Shadowed user-space task */
    struct thread_struct *tsp;	/* Pointer to the active thread struct (&ts or &user->thread). */

    /* Init block */
    struct xnthread *self;
    int imask;
    const char *name;
    void (*entry)(void *cookie);
    void *cookie;

} xnarchtcb_t;

typedef struct xnarch_fltinfo {

    unsigned exception;
    struct pt_regs *regs;

} xnarch_fltinfo_t;

#define xnarch_fault_trap(fi)   ((fi)->exception)
#define xnarch_fault_code(fi)   (0) /* None on this arch. */
#define xnarch_fault_pc(fi)     ((fi)->regs->retx)
#define xnarch_fault_fpu_p(fi)  (0) /* Can't be. */
/* The following predicates are only usable over a regular Linux stack
   context. */
#define xnarch_fault_pf_p(fi)   (0) /* No page faults. */
#define xnarch_fault_bp_p(fi)   ((current->ptrace & PT_PTRACED) && \
				 ((fi)->exception == VEC_STEP || \
				  (fi)->exception == VEC_EXCPT01 || \
				  (fi)->exception == VEC_WATCH))

#define xnarch_fault_notify(fi) (!xnarch_fault_bp_p(fi))

#ifdef __cplusplus
extern "C" {
#endif

static inline void *xnarch_sysalloc (u_long bytes)

{
    return kmalloc(bytes,GFP_KERNEL);
}

static inline void xnarch_sysfree (void *chunk, u_long bytes)

{
    kfree(chunk);
}

#ifdef XENO_POD_MODULE

void xnpod_welcome_thread(struct xnthread *, int);

void xnpod_delete_thread(struct xnthread *);

static inline int xnarch_start_timer (unsigned long ns,
				      void (*tickhandler)(void))
{
    return rthal_timer_request(tickhandler,ns);
}

static inline void xnarch_stop_timer (void)
{
    rthal_timer_release();
}

static inline void xnarch_leave_root (xnarchtcb_t *rootcb)

{
    /* Remember the preempted Linux task pointer. */
    rootcb->user_task = current;
    rootcb->tsp = &current->thread;
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb)

{
}

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb)
{
    if (in_tcb->user_task)
	rthal_clear_foreign_stack(&rthal_domain);
    else
	rthal_set_foreign_stack(&rthal_domain);

    rthal_thread_switch(out_tcb->tsp, in_tcb->tsp);
}

static inline void xnarch_finalize_and_switch (xnarchtcb_t *dead_tcb,
					       xnarchtcb_t *next_tcb)
{
    xnarch_switch_to(dead_tcb,next_tcb);
}

static inline void xnarch_finalize_no_switch (xnarchtcb_t *dead_tcb)

{
    /* Empty */
}

static inline void xnarch_init_root_tcb (xnarchtcb_t *tcb,
					 struct xnthread *thread,
					 const char *name)
{
    tcb->user_task = current;
    tcb->tsp = &tcb->ts;
    tcb->entry = NULL;
    tcb->cookie = NULL;
    tcb->self = thread;
    tcb->imask = 0;
    tcb->name = name;
}

asmlinkage static void xnarch_thread_trampoline (xnarchtcb_t *tcb)

{
    xnpod_welcome_thread(tcb->self, tcb->imask);
    tcb->entry(tcb->cookie);
    xnpod_delete_thread(tcb->self);
}

static inline void xnarch_init_thread (xnarchtcb_t *tcb,
				       void (*entry)(void *),
				       void *cookie,
				       int imask,
				       struct xnthread *thread,
				       char *name)
{
    unsigned long *ksp;

    ksp = (unsigned long *)(((unsigned long)tcb->stackbase + tcb->stacksize - 40) & ~0xf);
    ksp[0] = (unsigned long)tcb; /* r0 */
    memset(&ksp[1],0,sizeof(long) * 7); /* ( R7:4, P5:3 ) */
    ksp[8] = 0; /* fp */
    ksp[9] = (unsigned long)&xnarch_thread_trampoline; /* rets */
    
    tcb->ts.ksp = (unsigned long)ksp;
    tcb->ts.pc = (unsigned long)&rthal_thread_trampoline;
    tcb->ts.usp = 0;

    tcb->entry = entry;
    tcb->cookie = cookie;
    tcb->self = thread;
    tcb->imask = imask;
    tcb->name = name;
}

#define xnarch_fpu_init_p(task) (0)

static inline void xnarch_enable_fpu (xnarchtcb_t *current_tcb)

{
}

static inline void xnarch_init_fpu (xnarchtcb_t *tcb)

{
}

static inline void xnarch_save_fpu (xnarchtcb_t *tcb)

{
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
}

static inline int xnarch_escalate (void)

{
    extern int xnarch_escalation_virq;

    /* The following Blackfin-specific check is likely the most
     * braindamage stuff we need to do for this arch, i.e. deferring
     * Xenomai's rescheduling procedure whenever:

     * 1. ILAT tells us that a deferred syscall (EVT15) is pending, so
     * that we don't later execute this syscall over the wrong thread
     * context. This could happen whenever a user-space task (plain or
     * Xenomai) gets preempted by a high priority interrupt right
     * after the deferred syscall event is raised (EVT15) but before
     * the evt_system_call ISR could run. In case of deferred Xenomai
     * rescheduling, the pending rescheduling opportunity will be
     * checked at the beginning of Xenomai's do_hisyscall_event which
     * intercepts any incoming syscall, and we know it will happen
     * shortly after.
     *
     * 2. the context we will switch back to belongs to the Linux
     * kernel code, so that we don't inadvertently cause the CPU to
     * switch to user operating mode as a result of returning from an
     * interrupt stack frame over the incoming thread through RTI. In
     * the latter case, the preempted kernel code will be diverted
     * shortly before resumption in order to run the rescheduling
     * procedure (see __ipipe_irq_tail_hook).
     */

    if (rthal_defer_switch_p()) {
	__ipipe_lock_root();
	return 1;
    }

    __ipipe_unlock_root();

    if (rthal_current_domain == rthal_root_domain) {
        rthal_trigger_irq(xnarch_escalation_virq);
        return 1;
    }

    return 0;
}

#endif /* XENO_POD_MODULE */

#ifdef XENO_THREAD_MODULE

static inline void xnarch_init_tcb (xnarchtcb_t *tcb) {

    tcb->user_task = NULL;
    tcb->tsp = &tcb->ts;
    /* Must be followed by xnarch_init_thread(). */
}

#define xnarch_alloc_stack(tcb,stacksize) \
({ \
    int __err; \
    (tcb)->stacksize = stacksize; \
    if (stacksize == 0) { \
        (tcb)->stackbase = NULL; \
	__err = 0; \
    } else { \
        (tcb)->stackbase = xnmalloc(stacksize); \
        __err = (tcb)->stackbase ? 0 : -ENOMEM; \
    } \
    __err; \
})

#define xnarch_free_stack(tcb) \
do { \
      if ((tcb)->stackbase) \
	  xnfree((tcb)->stackbase); \
} while(0)

#endif /* XENO_THREAD_MODULE */

#ifdef XENO_SHADOW_MODULE

#include <asm/xenomai/syscall.h>

static inline void xnarch_init_shadow_tcb (xnarchtcb_t *tcb,
					   struct xnthread *thread,
					   const char *name)
{
    struct task_struct *task = current;

    tcb->user_task = task;
    tcb->tsp = &task->thread;
    tcb->entry = NULL;
    tcb->cookie = NULL;
    tcb->self = thread;
    tcb->imask = 0;
    tcb->name = name;
}

static inline void xnarch_grab_xirqs (rthal_irq_handler_t handler)

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	rthal_virtualize_irq(rthal_current_domain,
			     irq,
			     handler,
			     NULL,
			     NULL,
			     IPIPE_HANDLE_MASK);
}

static inline void xnarch_lock_xirqs (rthal_pipeline_stage_t *ipd, int cpuid)

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	{
	switch (irq)
	    {
#ifdef CONFIG_SMP
	    case RTHAL_CRITICAL_IPI:

		/* Never lock out this one. */
		continue;
#endif /* CONFIG_SMP */

	    default:

		rthal_lock_irq(ipd,cpuid,irq);
	    }
	}
}

static inline void xnarch_unlock_xirqs (rthal_pipeline_stage_t *ipd, int cpuid)

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	{
	switch (irq)
	    {
#ifdef CONFIG_SMP
	    case RTHAL_CRITICAL_IPI:

		continue;
#endif /* CONFIG_SMP */

	    default:

		rthal_unlock_irq(ipd,irq);
	    }
	}
}

static inline int xnarch_local_syscall (struct pt_regs *regs)
{
    unsigned long ptr, x, r, flags;
    int err = 0;

    local_irq_save_hw(flags);

    switch (__xn_reg_arg1(regs))
	{
	case __xn_lsys_xchg:

	    /* lsys_xchg(ptr,newval,&oldval) */
	    ptr = __xn_reg_arg2(regs);
	    x = __xn_reg_arg3(regs);
	    r = xchg((unsigned long *)ptr,x);
	    __xn_put_user(current,r,(unsigned long *)__xn_reg_arg4(regs));
	    break;

	default:

	    err = -ENOSYS;
	}

    local_irq_restore_hw(flags);

    return err;
}

#define xnarch_schedule_tail(prev) do { } while(0)

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static inline void xnarch_program_timer_shot (unsigned long delay)
{
    /* The core timer runs at the core clock rate -- therefore no
       conversion is needed between TSC and delay values. */
    rthal_timer_program_shot(delay);
#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY
    {
    extern unsigned long rthal_maxlat_tsc;
    delay = rthal_imuldiv(delay,RTHAL_NMICLK_FREQ,RTHAL_CPU_FREQ);
    if (delay <= ULONG_MAX - rthal_maxlat_tsc)
	rthal_nmi_arm(delay + rthal_maxlat_tsc);
    }
#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY */
}

static inline int xnarch_send_timer_ipi (xnarch_cpumask_t mask)

{
#ifdef CONFIG_SMP
    return -1;		/* FIXME */
#else /* ! CONFIG_SMP */
    return 0;
#endif /* CONFIG_SMP */
}

#endif /* XENO_TIMER_MODULE */

#ifdef XENO_INTR_MODULE

static inline void xnarch_relay_tick (void)
{
}

static inline void xnarch_announce_tick(void)
{
#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY
    rthal_nmi_disarm();
#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY */
}

#endif /* XENO_INTR_MODULE */

#ifdef XENO_MAIN_MODULE

#include <linux/init.h>
#include <asm/xenomai/calibration.h>

extern u_long nkschedlat;

extern u_long nktimerlat;

int xnarch_escalation_virq;

int xnpod_trap_fault(xnarch_fltinfo_t *fltinfo);

void xnpod_schedule_handler(void);

void xnpod_schedule_deferred(void);

static rthal_trap_handler_t xnarch_old_trap_handler;

static int xnarch_trap_fault (unsigned event, unsigned domid, void *data)
{
    xnarch_fltinfo_t fltinfo;
    fltinfo.exception = event;
    fltinfo.regs = (struct pt_regs *)data;
    return xnpod_trap_fault(&fltinfo);
}

unsigned long xnarch_calibrate_timer (void)

{
#if CONFIG_XENO_OPT_TIMING_TIMERLAT != 0
    return xnarch_ns_to_tsc(CONFIG_XENO_OPT_TIMING_TIMERLAT) ?: 1;
#else /* CONFIG_XENO_OPT_TIMING_TIMERLAT unspecified. */
    /* Compute the time needed to program the decrementer in aperiodic
       mode. The return value is expressed in timebase ticks. */
    return xnarch_ns_to_tsc(rthal_timer_calibrate()) ?: 1;
#endif /* CONFIG_XENO_OPT_TIMING_TIMERLAT != 0 */
}

int xnarch_calibrate_sched (void)

{
    nktimerlat = xnarch_calibrate_timer();

    if (!nktimerlat)
	return -ENODEV;

    nkschedlat = xnarch_ns_to_tsc(xnarch_get_sched_latency());

    return 0;
}

static inline int xnarch_init (void)

{
    int err;

    __ipipe_irq_tail_hook = (unsigned long)&xnpod_schedule_deferred;

    err = rthal_init();

    if (err)
	return err;

#ifdef CONFIG_SMP
    /* The HAL layer also sets the same CPU affinity so that both
       modules keep their execution sequence on SMP boxen. */
    set_cpus_allowed(current,cpumask_of_cpu(0));
#endif /* CONFIG_SMP */

    err = xnarch_calibrate_sched();

    if (err)
	return err;

    xnarch_escalation_virq = rthal_alloc_virq();

    if (xnarch_escalation_virq == 0)
	return -ENOSYS;

    rthal_virtualize_irq(&rthal_domain,
			 xnarch_escalation_virq,
			 (rthal_irq_handler_t)&xnpod_schedule_handler,
			 NULL,
			 NULL,
			 IPIPE_HANDLE_MASK | IPIPE_WIRED_MASK);

    xnarch_old_trap_handler = rthal_trap_catch(&xnarch_trap_fault);

    return 0;
}

static inline void xnarch_exit (void)

{
    __ipipe_irq_tail_hook = 0;
    rthal_trap_catch(xnarch_old_trap_handler);
    rthal_free_virq(xnarch_escalation_virq);
    rthal_exit();
}

#endif /* XENO_MAIN_MODULE */

#ifdef __cplusplus
}
#endif

#else /* !__KERNEL__ */

#include <nucleus/system.h>
#include <bits/local_lim.h>

#endif /* __KERNEL__ */

#endif /* !_XENO_ASM_BFINNOMMU_SYSTEM_H */
