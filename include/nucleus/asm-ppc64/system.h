/*
 * Xenomai 64-bit PowerPC adoption
 * Copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
 * based on previous work:
 *     
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_PPC64_SYSTEM_H
#define _XENO_ASM_PPC64_SYSTEM_H

#include <nucleus/asm-generic/system.h>

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/ptrace.h>

#ifdef CONFIG_ADEOS_CORE
#if ADEOS_RELEASE_NUMBER < 0x02060201
#error "Adeos 2.6r2c1/ppc64 or above is required to run this software; please upgrade."
#error "See http://download.gna.org/adeos/patches/v2.6/ppc64/"
#endif
#endif /* CONFIG_ADEOS_CORE */

#define XNARCH_DEFAULT_TICK     1000000 /* ns, i.e. 1ms */
#define XNARCH_HOST_TICK        (1000000000UL/HZ)

#define XNARCH_THREAD_STACKSZ   16384

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)

struct xnthread;
struct task_struct;

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    /* User mode side */
    struct task_struct *user_task;	/* Shadowed user-space task */
    struct task_struct *active_task;	/* Active user-space task */
    struct thread_struct *tsp;	/* Pointer to the active thread struct (&ts or &user->thread). */

    /* Kernel mode side */
    struct thread_struct ts;	/* Holds kernel-based thread context. */
#ifdef CONFIG_XENO_HW_FPU
    /* We only care for basic FPU handling in kernel-space; Altivec
       and SPE are not available to kernel-based nucleus threads. */
    rthal_fpenv_t fpuenv  __attribute__ ((aligned (16)));
    rthal_fpenv_t *fpup;	/* Pointer to the FPU backup area */
    struct task_struct *user_fpu_owner;
    unsigned long user_fpu_owner_prev_msr;
    /* Pointer the the FPU owner in userspace:
       - NULL for RT K threads,
       - last_task_used_math for Linux US threads (only current or NULL when MP)
       - current for RT US threads.
    */
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)
#else /* !CONFIG_XENO_HW_FPU */
#define xnarch_fpu_ptr(tcb)     NULL
#endif /* CONFIG_XENO_HW_FPU */

    unsigned stacksize;		/* Aligned size of stack (bytes) */
    unsigned long *stackbase;	/* Stack space */

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

#define xnarch_fault_trap(fi)   ((unsigned int)(fi)->regs->trap)
#define xnarch_fault_code(fi)   ((fi)->regs->dar)
#define xnarch_fault_pc(fi)     ((fi)->regs->nip)
#define xnarch_fault_pc(fi)     ((fi)->regs->nip)
/* FIXME: FPU faults ignored by the nanokernel on PPC. */
#define xnarch_fault_fpu_p(fi)  (0)

/* The following predicates are only usable over a regular Linux stack
 *    context. */
#ifdef CONFIG_ADEOS_CORE
#define xnarch_fault_pf_p(fi)   ((fi)->exception == ADEOS_ACCESS_TRAP)
#define xnarch_fault_bp_p(fi)   ((current->ptrace & PT_PTRACED) && \
				((fi)->exception == ADEOS_IABR_TRAP || \
				(fi)->exception == ADEOS_SSTEP_TRAP || \
				(fi)->exception == ADEOS_PERFMON_TRAP))
#else /* !CONFIG_ADEOS_CORE */
#define xnarch_fault_pf_p(fi)   ((fi)->exception == IPIPE_TRAP_ACCESS)
#define xnarch_fault_bp_p(fi)   ((current->ptrace & PT_PTRACED) && \
				((fi)->exception == IPIPE_TRAP_IABR || \
				(fi)->exception == IPIPE_TRAP_SSTEP || \
				(fi)->exception == IPIPE_TRAP_PERFMON))
#endif /* CONFIG_ADEOS_CORE */

#define xnarch_fault_notify(fi) (!xnarch_fault_bp_p(fi))

#ifdef __cplusplus
extern "C" {
#endif

static inline void *xnarch_sysalloc (u_long bytes)

{
#if 0	/* FIXME: likely on-demand mapping bug here */
    if (bytes >= 128*1024)
	return vmalloc(bytes);
#endif

    return kmalloc(bytes,GFP_KERNEL);
}

static inline void xnarch_sysfree (void *chunk, u_long bytes)

{
#if 0	/* FIXME: likely on-demand mapping bug here */
    if (bytes >= 128*1024)
	vfree(chunk);
    else
#endif
	kfree(chunk);
}

static inline void xnarch_relay_tick (void)

{
    rthal_irq_host_pend(RTHAL_TIMER_IRQ);
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
    rootcb->tsp = &rootcb->user_task->thread;
#ifdef CONFIG_XENO_HW_FPU
    rootcb->user_fpu_owner = rthal_get_fpu_owner(rootcb->user_task);
    /* So that xnarch_save_fpu() will operate on the right FPU area. */
    rootcb->fpup = (rootcb->user_fpu_owner
                    ? (rthal_fpenv_t *)&rootcb->user_fpu_owner->thread.fpr[0]
                    : NULL);
#endif /* CONFIG_XENO_HW_FPU */
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb)
{
    __clear_bit(xnarch_current_cpu(),&rthal_cpu_realtime);
}

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb)
{
    struct task_struct *prev = out_tcb->active_task;
    struct task_struct *next = in_tcb->user_task;

    in_tcb->active_task = next ?: prev;

    if (next && next != prev) /* Switch to new user-space thread? */
	{
	struct mm_struct *mm = next->active_mm;

	/* Switch the mm context.*/

#ifdef CONFIG_ALTIVEC
	asm volatile (
		      "dssall;\n"
		      : : );
#endif /* CONFIG_ALTIVEC */
	
	if (!cpu_isset(smp_processor_id(), mm->cpu_vm_mask)) {
	    cpu_set(smp_processor_id(), mm->cpu_vm_mask);
	}
	
	if (cur_cpu_spec->cpu_features & CPU_FTR_SLB) {
	    switch_slb(next, mm);
	}
	else {
	    switch_stab(next, mm);
	}
	
	flush_tlb_pending();
	}

    rthal_thread_switch(out_tcb->tsp, in_tcb->tsp, 
		    in_tcb->user_task == NULL ? 1 : 0);
    
    barrier();
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
    tcb->tsp = &tcb->ts;
#ifdef CONFIG_XENO_HW_FPU
    tcb->user_fpu_owner = NULL;
    tcb->fpup = NULL;
#endif /* CONFIG_XENO_HW_FPU */
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
    unsigned long *ksp, flags;
    struct pt_regs *childregs;

    rthal_local_irq_flags_hw(flags);

    ksp = (unsigned long *)((unsigned long)tcb->stackbase + tcb->stacksize - RTHAL_SWITCH_FRAME_SIZE - 32);
    childregs = (struct pt_regs *)ksp;
    memset(childregs,0,sizeof(*childregs));
    childregs->nip = ((unsigned long *)&rthal_thread_trampoline)[0];
    childregs->gpr[2] = ((unsigned long *)&rthal_thread_trampoline)[1];
    childregs->gpr[14] = flags & ~(MSR_EE | MSR_FP);
    childregs->gpr[15] = ((unsigned long *)&xnarch_thread_trampoline)[0]; /* lr = entry addr. */
    childregs->gpr[16] = ((unsigned long *)&xnarch_thread_trampoline)[1]; /* r2 = TOC base. */
    childregs->gpr[17] = (unsigned long)tcb;
    tcb->ts.ksp = (unsigned long)childregs - STACK_FRAME_OVERHEAD;
    if (cpu_has_feature(CPU_FTR_SLB)) { /* from process.c/copy_thread */
	    unsigned long sp_vsid = get_kernel_vsid(tcb->ts.ksp);
	    
	    sp_vsid <<= SLB_VSID_SHIFT;
	    sp_vsid |= SLB_VSID_KERNEL;
	    if (cpu_has_feature(CPU_FTR_16M_PAGE))
		    sp_vsid |= SLB_VSID_L;
	    
	    tcb->ts.ksp_vsid = sp_vsid;
    }
    
    tcb->entry = entry;
    tcb->cookie = cookie;
    tcb->self = thread;
    tcb->imask = imask;
    tcb->name = name;
}

/* No lazy FPU init on PPC. */
#define xnarch_fpu_init_p(task) (1)

static inline void xnarch_enable_fpu (xnarchtcb_t *current_tcb)

{
#ifdef CONFIG_XENO_HW_FPU
    if(!current_tcb->user_task)
        rthal_enable_fpu();
#endif /* CONFIG_XENO_HW_FPU */
}

static inline void xnarch_init_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_XENO_HW_FPU
    /* Initialize the FPU for an emerging kernel-based RT thread. This
       must be run on behalf of the emerging thread. */
    memset(&tcb->fpuenv,0,sizeof(tcb->fpuenv));
    rthal_init_fpu(&tcb->fpuenv);
#endif /* CONFIG_XENO_HW_FPU */
}

static inline void xnarch_save_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_XENO_HW_FPU

    if(tcb->fpup)
        {
        rthal_save_fpu(tcb->fpup);

        if(tcb->user_fpu_owner && tcb->user_fpu_owner->thread.regs)
            {
            tcb->user_fpu_owner_prev_msr = tcb->user_fpu_owner->thread.regs->msr;
            tcb->user_fpu_owner->thread.regs->msr &= ~MSR_FP;
            }
        }   

#endif /* CONFIG_XENO_HW_FPU */
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_XENO_HW_FPU

    if(tcb->fpup)
        {
        rthal_restore_fpu(tcb->fpup);

	/* Note: Only enable FP in MSR, if it was enabled when we saved the
	 * fpu state. We might have preempted Linux when it had disabled FP
	 * for the thread, but not yet set last_task_used_math to NULL 
	 */
        if(tcb->user_fpu_owner && 
			tcb->user_fpu_owner->thread.regs &&
			((tcb->user_fpu_owner_prev_msr & MSR_FP) != 0))
            tcb->user_fpu_owner->thread.regs->msr |= MSR_FP;
        }   

    /* FIXME: We restore FPU "as it was" when Xenomai preempted Linux,
       whereas we could be much lazier. */
    if(tcb->user_task)
        rthal_disable_fpu();

#endif /* CONFIG_XENO_HW_FPU */
}

#endif /* XENO_POD_MODULE */

#ifdef XENO_THREAD_MODULE

static inline void xnarch_init_tcb (xnarchtcb_t *tcb) {

    tcb->user_task = NULL;
    tcb->active_task = NULL;
    tcb->tsp = &tcb->ts;
    memset(&tcb->ts,0,sizeof(tcb->ts));
#ifdef CONFIG_XENO_HW_FPU
    tcb->user_fpu_owner = NULL;
    tcb->fpup = &tcb->fpuenv;
#endif /* CONFIG_XENO_HW_FPU */
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

static inline void xnarch_init_shadow_tcb (xnarchtcb_t *tcb,
					   struct xnthread *thread,
					   const char *name)
{
    struct task_struct *task = current;

    tcb->user_task = task;
    tcb->active_task = NULL;
    tcb->tsp = &task->thread;
#ifdef CONFIG_XENO_HW_FPU
    tcb->user_fpu_owner = task;
    tcb->fpup = (rthal_fpenv_t *)&task->thread.fpr[0];
#endif /* CONFIG_XENO_HW_FPU */
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

    /* On this arch, the decrementer trap is not an external IRQ but
       it is instead mapped to a virtual IRQ, so we must grab it
       individually. */

    rthal_virtualize_irq(rthal_current_domain,
			 RTHAL_TIMER_IRQ,
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

    rthal_lock_irq(ipd,cpuid,RTHAL_TIMER_IRQ);
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

    rthal_unlock_irq(ipd,RTHAL_TIMER_IRQ);
}

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static inline void xnarch_program_timer_shot (unsigned long delay) {
    /* Even though some architectures may use a 64 bits delay here, we
       voluntarily limit to 32 bits, 4 billions ticks should be enough
       for now. Would a timer needs more, an extra call to the tick
       handler would simply occur after 4 billions ticks.  Since the
       timebase value is used to express CPU ticks on the PowerPC
       port, there is no need to rescale the delay value. */
    rthal_timer_program_shot(delay);
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

#ifdef XENO_MAIN_MODULE

#include <linux/init.h>
#include <nucleus/asm/calibration.h>

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
}

#endif /* XENO_MAIN_MODULE */

#ifdef __cplusplus
}
#endif

#else /* !__KERNEL__ */

#include <nucleus/system.h>
#include <bits/local_lim.h>

#endif /* __KERNEL__ */

#endif /* !_XENO_ASM_PPC64_SYSTEM_H */
