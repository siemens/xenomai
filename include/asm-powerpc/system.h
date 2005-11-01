/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * 64-bit PowerPC adoption
 *   copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
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

#ifndef _XENO_ASM_POWERPC_SYSTEM_H
#define _XENO_ASM_POWERPC_SYSTEM_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/ptrace.h>
#include <asm-generic/xenomai/system.h>

#ifdef CONFIG_ADEOS_CORE
#ifdef CONFIG_PPC64
#if ADEOS_RELEASE_NUMBER < 0x02060201
#error "Adeos 2.6r2c1/ppc64 or above is required to run this software; please upgrade."
#error "See http://download.gna.org/adeos/patches/v2.6/ppc64/"
#endif
#else /* !CONFIG_PPC64 */
#if ADEOS_RELEASE_NUMBER < 0x02060703
#error "Adeos 2.6r7c3/ppc or above is required to run this software; please upgrade."
#error "See http://download.gna.org/adeos/patches/v2.6/ppc/"
#endif
#endif /* CONFIG_PPC64 */
#endif /* CONFIG_ADEOS_CORE */

#define XNARCH_DEFAULT_TICK     1000000 /* ns, i.e. 1ms */
#define XNARCH_HOST_TICK        (1000000000UL/HZ)

#ifdef CONFIG_PPC64
#define XNARCH_THREAD_STACKSZ   16384
#else
#define XNARCH_THREAD_STACKSZ   4096
#endif

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)

#define xnarch_alloc_stack xnmalloc
#define xnarch_free_stack  xnfree

struct xnthread;
struct task_struct;

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    /* Kernel mode side */

#ifdef CONFIG_XENO_HW_FPU
    /* We only care for basic FPU handling in kernel-space; Altivec
       and SPE are not available to kernel-based nucleus threads. */
    rthal_fpenv_t fpuenv  __attribute__ ((aligned (16)));
    rthal_fpenv_t *fpup;	/* Pointer to the FPU backup area */
    struct task_struct *user_fpu_owner;
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

#define xnarch_fault_trap(fi)   ((unsigned int)(fi)->regs->trap)
#define xnarch_fault_code(fi)   ((fi)->regs->dar)
#define xnarch_fault_pc(fi)     ((fi)->regs->nip)
#define xnarch_fault_pc(fi)     ((fi)->regs->nip)
/* FIXME: FPU faults ignored by the nanokernel on PPC. */
#define xnarch_fault_fpu_p(fi)  (0)
/* The following predicates are only usable over a regular Linux stack
   context. */
#ifdef CONFIG_ADEOS_CORE
#define xnarch_fault_pf_p(fi)   ((fi)->exception == ADEOS_ACCESS_TRAP)
#ifdef CONFIG_PPC64
#define xnarch_fault_bp_p(fi)   ((current->ptrace & PT_PTRACED) && \
				 ((fi)->exception == ADEOS_IABR_TRAP || \
				  (fi)->exception == ADEOS_SSTEP_TRAP || \
				  (fi)->exception == ADEOS_PERFMON_TRAP))
#else /* !CONFIG_PPC64 */
#define xnarch_fault_bp_p(fi)   ((current->ptrace & PT_PTRACED) && \
				 ((fi)->exception == ADEOS_IABR_TRAP || \
				  (fi)->exception == ADEOS_SSTEP_TRAP || \
				  (fi)->exception == ADEOS_DEBUG_TRAP))
#endif /* CONFIG_PPC64 */
#else /* !CONFIG_ADEOS_CORE */
#define xnarch_fault_pf_p(fi)   ((fi)->exception == IPIPE_TRAP_ACCESS)
#define xnarch_fault_bp_p(fi)   ((current->ptrace & PT_PTRACED) && \
				 ((fi)->exception == IPIPE_TRAP_IABR || \
				  (fi)->exception == IPIPE_TRAP_SSTEP || \
				  (fi)->exception == IPIPE_TRAP_DEBUG))
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
#ifdef CONFIG_XENO_HW_FPU
    rootcb->user_fpu_owner = rthal_get_fpu_owner(rootcb->user_task);
    /* So that xnarch_save_fpu() will operate on the right FPU area. */
    rootcb->fpup = (rootcb->user_fpu_owner
                    ? (rthal_fpenv_t *)&rootcb->user_fpu_owner->thread.fpr[0]
                    : NULL);
#endif /* CONFIG_XENO_HW_FPU */
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb) {
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

#ifdef CONFIG_PPC64
#ifdef CONFIG_ALTIVEC
	/* Don't rely on FTR fixups --
	   they don't work properly in our context. */
	if (cur_cpu_spec->cpu_features & CPU_FTR_ALTIVEC) {
	    asm volatile (
		"dssall;\n"
		: : );
	}
#endif /* CONFIG_ALTIVEC */
#else /* !CONFIG_PPC64 */
#ifdef CONFIG_ALTIVEC
	/* Don't rely on FTR fixups --
	   they don't work properly in our context. */
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_ALTIVEC) {
	    asm volatile (
		"dssall;\n"
#ifndef CONFIG_POWER4
		"sync;\n"
#endif
		: : );
	}
#endif /* CONFIG_ALTIVEC */
#endif /* CONFIG_PPC64 */
	
#ifdef CONFIG_PPC64
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
#else /* !CONFIG_PPC64 */
	next->thread.pgdir = mm->pgd;
	get_mmu_context(mm);
	set_context(mm->context,mm->pgd);

	/* _switch expects a valid "current" (r2) for storing
	 * ALTIVEC and SPE state. */
	current = prev;
#endif /* CONFIG_PPC64 */
	_switch(&prev->thread, &next->thread);

	barrier();
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

    rthal_local_irq_flags_hw(flags);

#ifdef CONFIG_PPC64
    if (tcb->stackbase) {
	*tcb->stackbase = 0;
	
	ksp = (unsigned long *)(((unsigned long)tcb->stackbase + tcb->stacksize - 16) & ~0xf);
	*ksp = 0L; /* first stack frame back-chain */
	ksp = ksp - STACK_FRAME_OVERHEAD; /* first stack frame (entry uses) */
	*ksp = (unsigned long)ksp+STACK_FRAME_OVERHEAD; /* second back-chain */
	ksp = ksp - RTHAL_SWITCH_FRAME_SIZE; /* domain context */
	tcb->ksp = (unsigned long)ksp - STACK_FRAME_OVERHEAD;
	*((unsigned long *)tcb->ksp) = (unsigned long)ksp + 224; /*back-chain*/
	/* NOTE: these depend on rthal_switch_context ordering */
	ksp[18] = (unsigned long)get_paca(); /* r13 needs to hold paca */
	ksp[19] = (unsigned long)tcb; /* r3 */
	ksp[20] = ((unsigned long *)&xnarch_thread_trampoline)[1]; /* r2 = TOC base */
	ksp[25] = ((unsigned long *)&xnarch_thread_trampoline)[0]; /* lr = entry addr. */
	ksp[26] = flags & ~(MSR_EE | MSR_FP); /* msr */
    }
    else {
	printk("xnarch_init_thread: NULL stackbase!\n");
    }
#else /* !CONFIG_PPC64 */
    *tcb->stackbase = 0;
    ksp = (unsigned long *)((((unsigned long)tcb->stackbase + tcb->stacksize - 0x10) & ~0xf)
			    - RTHAL_SWITCH_FRAME_SIZE);
    tcb->ksp = (unsigned long)ksp - STACK_FRAME_OVERHEAD;
    ksp[19] = (unsigned long)tcb; /* r3 */
    ksp[25] = (unsigned long)&xnarch_thread_trampoline; /* lr */
    ksp[26] = flags & ~(MSR_EE | MSR_FP); /* msr */
#endif
    
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
            tcb->user_fpu_owner->thread.regs->msr &= ~MSR_FP;
        }   

#endif /* CONFIG_XENO_HW_FPU */
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_XENO_HW_FPU

    if(tcb->fpup)
        {
        rthal_restore_fpu(tcb->fpup);

        if(tcb->user_fpu_owner && tcb->user_fpu_owner->thread.regs)
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
    tcb->kspp = &tcb->ksp;
#ifdef CONFIG_XENO_HW_FPU
    tcb->user_fpu_owner = NULL;
    tcb->fpup = &tcb->fpuenv;
#endif /* CONFIG_XENO_HW_FPU */
    /* Must be followed by xnarch_init_thread(). */
}

#endif /* XENO_THREAD_MODULE */

#ifdef XENO_SHADOW_MODULE

static inline void xnarch_init_shadow_tcb (xnarchtcb_t *tcb,
					   struct xnthread *thread,
					   const char *name)
{
    struct task_struct *task = current;

    tcb->user_task = task;
    tcb->active_task = NULL;
    tcb->ksp = 0;
    tcb->kspp = &task->thread.ksp;
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

#endif /* !_XENO_ASM_POWERPC_SYSTEM_H */
