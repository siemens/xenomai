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

#define XNARCH_DEFAULT_TICK     1000000 /* ns, i.e. 1ms */
#ifdef CONFIG_XENO_HW_PERIODIC_TIMER
/* If the periodic timing support is compiled in, we need a dynamic
   information about the current timer mode in order to determine the
   hist tick setup. Ask the HAL for this. */
#define XNARCH_HOST_TICK        rthal_timer_host_freq()
#else /* !CONFIG_XENO_HW_PERIODIC_TIMER */
/* If the periodic timing support is not compiled in, we need to relay
   the host tick in any case; just define the period constant. */
#define XNARCH_HOST_TICK        RTHAL_HOST_PERIOD
#endif /* CONFIG_XENO_HW_PERIODIC_TIMER */

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
    unsigned long ksp;		/* Saved KSP for kernel-based threads */
    unsigned long *kspp;	/* Pointer to saved KSP (&ksp or &user->thread.ksp) */

    /* User mode side */
    struct task_struct *user_task;	/* Shadowed user-space task */
    struct task_struct *active_task;	/* Active user-space task */

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

void xnpod_welcome_thread(struct xnthread *);

void xnpod_delete_thread(struct xnthread *);

static inline int xnarch_start_timer (unsigned long ns,
				      void (*tickhandler)(void))
{
    return rthal_timer_request(tickhandler,ns);
}

static inline void xnarch_leave_root (xnarchtcb_t *rootcb)

{
    rthal_declare_cpuid;

    rthal_load_cpuid();

    /* rthal_cpu_realtime is only tested for the current processor,
       and always inside a critical section. */
    __set_bit(cpuid,&rthal_cpu_realtime);
    /* Remember the preempted Linux task pointer. */
    rootcb->user_task = rootcb->active_task = rthal_current_host_task(cpuid);
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb) {
    __clear_bit(xnarch_current_cpu(),&rthal_cpu_realtime);
}

asmlinkage void resume(void);
#define __do_switch_to(prev,next) do { \
  __asm__ __volatile__(							\
  			"[--sp] = r4;\n\t"				\
  			"[--sp] = r5;\n\t"				\
  			"[--sp] = r6;\n\t"				\
  			"[--sp] = r7;\n\t"				\
  			"[--sp] = p3;\n\t"				\
  			"[--sp] = p4;\n\t"				\
  			"[--sp] = p5;\n\t"				\
  			"r0 = %0;\n\t"					\
			"r1 = %1;\n\t"					\
			"call resume;\n\t" 				\
  			"p5 = [sp++];\n\t"				\
  			"p4 = [sp++];\n\t"				\
  			"p3 = [sp++];\n\t"				\
  			"r7 = [sp++];\n\t"				\
  			"r6 = [sp++];\n\t"				\
  			"r5 = [sp++];\n\t"				\
  			"r4 = [sp++];\n\t"				\
			: /*no output*/					\
			: "d" (prev),					\
			  "d" (next)					\
			: "CC", "R0", "R1", "P0", "P1");		\
} while(0)

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb)
{
    struct task_struct *prev = out_tcb->active_task;
    struct task_struct *next = in_tcb->user_task;

    in_tcb->active_task = next ?: prev;

    if (next && next != prev) {
	/* Switch to user-space thread. */
	__do_switch_to(prev, next);
    }
    else
        /* Kernel-to-kernel context switch. */
        rthal_switch_context(out_tcb->kspp,in_tcb->kspp);
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
    tcb->active_task = NULL;
    tcb->ksp = 0;
    tcb->kspp = &tcb->ksp;
    tcb->entry = NULL;
    tcb->cookie = NULL;
    tcb->self = thread;
    tcb->imask = 0;
    tcb->name = name;
}

asmlinkage static void xnarch_thread_trampoline (xnarchtcb_t *tcb)

{
    rthal_local_irq_restore(!!tcb->imask);
    xnpod_welcome_thread(tcb->self);
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
    struct pt_regs regs;
    unsigned long *ksp;

    memset(&regs, 0, sizeof(regs));
    regs.r0 = (unsigned long)tcb;
    regs.pc = (unsigned long)&xnarch_thread_trampoline;
    regs.ipend = 0x8002;
    __asm__ __volatile__ ("%0 = syscfg;" : "=da" (regs.syscfg) : );

    ksp = (unsigned long *)((unsigned long)tcb->stackbase + tcb->stacksize - sizeof(regs));
    tcb->ksp = (unsigned long)ksp;
    memcpy(ksp,&regs,sizeof(regs));
    
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

#endif /* XENO_POD_MODULE */

#ifdef XENO_THREAD_MODULE

static inline void xnarch_init_tcb (xnarchtcb_t *tcb) {

    tcb->user_task = NULL;
    tcb->active_task = NULL;
    tcb->kspp = &tcb->ksp;
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
    tcb->active_task = NULL;
    tcb->ksp = 0;
    tcb->kspp = &task->thread.ksp;
    tcb->entry = NULL;
    tcb->cookie = NULL;
    tcb->self = thread;
    tcb->imask = 0;
    tcb->name = name;
}

static inline void xnarch_grab_xirqs (void (*handler)(unsigned irq))

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	rthal_virtualize_irq(rthal_current_domain,
			     irq,
			     handler,
			     NULL,
			     IPIPE_DYNAMIC_MASK);
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

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static inline void xnarch_program_timer_shot (unsigned long delay) {
    rthal_timer_program_shot(rthal_imuldiv(delay,RTHAL_TIMER_FREQ,RTHAL_CPU_FREQ));
}

static inline void xnarch_stop_timer (void) {
    rthal_timer_release();
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
    rthal_irq_host_pend(IRQ_CORETMR);
}

static inline void xnarch_announce_tick(unsigned irq)
{
    if (irq == RTHAL_ONESHOT_TIMER_IRQ)
	rthal_timer_clear_tick();
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
#if CONFIG_XENO_HW_TIMER_LATENCY != 0
    return xnarch_ns_to_tsc(CONFIG_XENO_HW_TIMER_LATENCY) ?: 1;
#else /* CONFIG_XENO_HW_TIMER_LATENCY unspecified. */
    /* Compute the time needed to program the decrementer in aperiodic
       mode. The return value is expressed in timebase ticks. */
    return xnarch_ns_to_tsc(rthal_timer_calibrate()) ?: 1;
#endif /* CONFIG_XENO_HW_TIMER_LATENCY != 0 */
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
			 (void (*)(unsigned))&xnpod_schedule_handler,
			 NULL,
			 IPIPE_HANDLE_MASK);

    xnarch_old_trap_handler = rthal_trap_catch(&xnarch_trap_fault);

#ifdef CONFIG_XENO_OPT_PERVASIVE
    err = xnshadow_mount();
#endif /* CONFIG_XENO_OPT_PERVASIVE */

    if (err)
	{
	rthal_trap_catch(xnarch_old_trap_handler);
        rthal_free_virq(xnarch_escalation_virq);
	}

    return err;
}

static inline void xnarch_exit (void)

{
#ifdef CONFIG_XENO_OPT_PERVASIVE
    xnshadow_cleanup();
#endif /* CONFIG_XENO_OPT_PERVASIVE */
    rthal_trap_catch(xnarch_old_trap_handler);
    rthal_free_virq(xnarch_escalation_virq);
    rthal_exit();
}

#endif /* XENO_MAIN_MODULE */

#ifdef __cplusplus
}
#endif

#else /* !__KERNEL__ */

#include <xenomai/nucleus/system.h>
#include <bits/local_lim.h>

#endif /* __KERNEL__ */

#endif /* !_XENO_ASM_BFINNOMMU_SYSTEM_H */
