/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
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

#ifndef _XENO_ASM_ARM_SYSTEM_H
#define _XENO_ASM_ARM_SYSTEM_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/ptrace.h>
#include <asm-generic/xenomai/system.h>
#include <asm/xenomai/syscall.h>

#define XNARCH_DEFAULT_TICK     1000000 /* ns, i.e. 1ms */
#define XNARCH_HOST_TICK        (1000000000UL/HZ)

#define XNARCH_THREAD_STACKSZ   4096

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)

struct xnthread;
struct task_struct;

typedef struct xnarchtcb {  /* Per-thread arch-dependent block */

    /* Kernel mode side */

#ifdef CONFIG_XENO_HW_FPU
    rthal_fpenv_t fpuenv;
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

    unsigned stacksize;         /* Aligned size of stack (bytes) */
    unsigned long *stackbase;   /* Stack space */

    /* User mode side */
    struct task_struct *user_task;      /* Shadowed user-space task */
    struct task_struct *active_task;    /* Active user-space task */
    struct thread_info ti;              /* Holds kernel-based thread info */
    struct thread_info *tip;            /* Pointer to the active thread info (ti or user->thread_info). */

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

#define xnarch_fault_trap(fi)   (0)
#define xnarch_fault_code(fi)   (0)
#define xnarch_fault_pc(fi)     ((fi)->regs->ARM_pc - (thumb_mode((fi)->regs) ? 2 : 4)) /* XXX ? */
#define xnarch_fault_fpu_p(fi)  (0)
/* The following predicates are only usable over a regular Linux stack
   context. */
#define xnarch_fault_pf_p(fi)   ((fi)->exception == IPIPE_TRAP_ACCESS)
#define xnarch_fault_bp_p(fi)   ((current->ptrace & PT_PTRACED) && \
                                ((fi)->exception == IPIPE_TRAP_BREAK))

#define xnarch_fault_notify(fi) (!xnarch_fault_bp_p(fi))

#ifdef __cplusplus
extern "C" {
#endif

static inline void *xnarch_sysalloc (u_long bytes)
{
    if (bytes > 128*1024)
	return vmalloc(bytes);

    return kmalloc(bytes,GFP_KERNEL);
}

static inline void xnarch_sysfree (void *chunk, u_long bytes)
{
    if (bytes > 128*1024)
	vfree(chunk);
    else
	kfree(chunk);
}

#ifdef XENO_POD_MODULE

#include <asm/xenomai/system.h>

void xnpod_welcome_thread(struct xnthread *);

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
    rthal_declare_cpuid;

    rthal_load_cpuid();

    /* rthal_cpu_realtime is only tested for the current processor,
       and always inside a critical section. */
    __set_bit(cpuid,&rthal_cpu_realtime);
    /* Remember the preempted Linux task pointer. */
    rootcb->user_task = rootcb->active_task = rthal_current_host_task(cpuid);
    rootcb->tip = current->thread_info;
#ifdef CONFIG_XENO_HW_FPU
    rootcb->user_fpu_owner = rthal_get_fpu_owner(rootcb->user_task);
    /* So that xnarch_save_fpu() will operate on the right FPU area. */
    rootcb->fpup = (rootcb->user_fpu_owner
                    ? (rthal_fpenv_t *)&rootcb->user_fpu_owner->thread_info->used_cp[0]
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

    if (next && next != prev) {
        /* Switch to new user-space thread? */
        struct mm_struct *oldmm = prev->active_mm;

        if (next->active_mm)
            switch_mm(oldmm, next->active_mm, next);
        if (!next->mm)
            enter_lazy_tlb(oldmm, next);
    }

    /* Kernel-to-kernel context switch. */
    rthal_thread_switch(out_tcb->tip, in_tcb->tip);
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
    tcb->tip = &tcb->ti;
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
    unsigned long flags;
    struct cpu_context_save *regs;

    rthal_local_irq_flags_hw(flags);

    memset(tcb->stackbase, 0, tcb->stacksize);

    regs = &tcb->ti.cpu_context;
    memset(regs, 0, sizeof(*regs));
    regs->pc = (unsigned long)&rthal_thread_trampoline;
    regs->r4 = (unsigned long)&xnarch_thread_trampoline;
    regs->r5 = (unsigned long)tcb;
    regs->sp = (unsigned long)tcb->stackbase + tcb->stacksize;

    tcb->entry = entry;
    tcb->cookie = cookie;
    tcb->self = thread;
    tcb->imask = imask;
    tcb->name = name;
}

/* No lazy FPU init on ARM. */
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

    if(tcb->fpup) {
        rthal_save_fpu(tcb->fpup);

        if(tcb->user_fpu_owner && tcb->user_fpu_owner->thread_info) {
            tcb->user_fpu_owner->thread_info->used_cp[1] = 0;
            tcb->user_fpu_owner->thread_info->used_cp[2] = 0;
        }
    }
#endif /* CONFIG_XENO_HW_FPU */
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)
{
#ifdef CONFIG_XENO_HW_FPU

    if(tcb->fpup) {
        rthal_restore_fpu(tcb->fpup);

        if(tcb->user_fpu_owner && tcb->user_fpu_owner->thread_info) {
            tcb->user_fpu_owner->thread_info->used_cp[1] = 1;
            tcb->user_fpu_owner->thread_info->used_cp[2] = 1;
        }
    }

    /* FIXME: We restore FPU "as it was" when Xenomai preempted Linux,
       whereas we could be much lazier. */
    if(tcb->user_task)
        rthal_disable_fpu();
#endif /* CONFIG_XENO_HW_FPU */
}

static inline int xnarch_escalate (void)
{
    extern int xnarch_escalation_virq;

    if (rthal_current_domain == rthal_root_domain) {
        rthal_trigger_irq(xnarch_escalation_virq);
        return 1;
    }

    return 0;
}

#endif /* XENO_POD_MODULE */

#ifdef XENO_THREAD_MODULE

static inline unsigned long xnarch_current_domain_access_control(void)
{
    unsigned long domain_access_control;
    asm("mrc p15, 0, %0, c3, c0" : "=r" (domain_access_control));
    return domain_access_control;
}

static inline void xnarch_init_tcb (xnarchtcb_t *tcb) {

    tcb->user_task = NULL;
    tcb->active_task = NULL;
    tcb->tip = &tcb->ti;
    tcb->ti.tp_value = 0;
    tcb->ti.cpu_domain = xnarch_current_domain_access_control();
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
    tcb->tip = task->thread_info;
#ifdef CONFIG_XENO_HW_FPU
    tcb->user_fpu_owner = task;
    tcb->fpup = (rthal_fpenv_t *)&task->thread_info->used_cp[0];
#endif /* CONFIG_XENO_HW_FPU */
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
        rthal_lock_irq(ipd,cpuid,irq);

}

static inline void xnarch_unlock_xirqs (rthal_pipeline_stage_t *ipd, int cpuid)
{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
        rthal_unlock_irq(ipd,irq);
}

static inline int xnarch_local_syscall (struct pt_regs *regs)
{
    int error = 0;

    switch (__xn_reg_arg1(regs)) {
    case XENOMAI_SYSARCH_ATOMIC_ADD_RETURN: {
        int i;
        atomic_t *v, val;
        int ret;
        unsigned long flags;

        local_irq_save_hw(flags);
        __xn_get_user(current, i, (int *)__xn_reg_arg2(regs));
        __xn_get_user(current, v, (atomic_t **)__xn_reg_arg3(regs));
        __xn_copy_from_user(current, &val, v, sizeof(atomic_t));
        ret = atomic_add_return(i, &val);
        __xn_copy_to_user(current, v, &val, sizeof(atomic_t));
        __xn_put_user(current, ret, (int *)__xn_reg_arg4(regs));
        local_irq_restore_hw(flags);
        break;
    }
    case XENOMAI_SYSARCH_ATOMIC_SET_MASK: {
        unsigned long mask;
        unsigned long *addr, val;
        unsigned long flags;

        local_irq_save_hw(flags);
        __xn_get_user(current, mask, (unsigned long *)__xn_reg_arg2(regs));
        __xn_get_user(current, addr, (unsigned long **)__xn_reg_arg3(regs));
        __xn_get_user(current, val, (unsigned long *)addr);
        val |= mask;
        __xn_put_user(current, val, (unsigned long *)addr);
        local_irq_restore_hw(flags);
        break;
    }
    case XENOMAI_SYSARCH_ATOMIC_CLEAR_MASK: {
        unsigned long mask;
        unsigned long *addr, val;
        unsigned long flags;

        local_irq_save_hw(flags);
        __xn_get_user(current, mask, (unsigned long *)__xn_reg_arg2(regs));
        __xn_get_user(current, addr, (unsigned long **)__xn_reg_arg3(regs));
        __xn_get_user(current, val, (unsigned long *)addr);
        val &= ~mask;
        __xn_put_user(current, val, (unsigned long *)addr);
        local_irq_restore_hw(flags);
        break;
    }
    case XENOMAI_SYSARCH_XCHG: {
        void *ptr;
        unsigned long x;
        unsigned int size;
        unsigned long ret = 0;
        unsigned long flags;

        local_irq_save_hw(flags);
        __xn_get_user(current, ptr, (unsigned char **)__xn_reg_arg2(regs));
        __xn_get_user(current, x, (unsigned long *)__xn_reg_arg3(regs));
        __xn_get_user(current, size, (unsigned int *)__xn_reg_arg4(regs));
        if (size == 4) {
            unsigned long val;
            __xn_get_user(current, val, (unsigned long *)ptr);
            ret = xnarch_atomic_xchg(&val, x);
        }
        else
            error = -EINVAL;
        __xn_put_user(current, ret, (unsigned long *)__xn_reg_arg5(regs));
        local_irq_restore_hw(flags);
        break;
    }
    default:
        error = -EINVAL;
    }
    return error;
}
#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static inline void xnarch_program_timer_shot (unsigned long delay) {
    rthal_timer_program_shot(rthal_imuldiv(delay,RTHAL_TIMER_FREQ,RTHAL_CPU_FREQ));
}

static inline int xnarch_send_timer_ipi (xnarch_cpumask_t mask)
{
    return 0;
}

#endif /* XENO_TIMER_MODULE */

#ifdef XENO_INTR_MODULE

static inline void xnarch_relay_tick (void)
{
    rthal_irq_host_pend(RTHAL_TIMER_IRQ);
}

static inline void xnarch_announce_tick(void)
{
    /* empty */
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

    err = rthal_init();

    if (err)
        return err;

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

#ifdef CONFIG_XENO_OPT_PERVASIVE
    err = xnshadow_mount();
#endif /* CONFIG_XENO_OPT_PERVASIVE */

    if (err) {
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

#include <nucleus/system.h>
#include <bits/local_lim.h>

#endif /* __KERNEL__ */

#endif /* !_XENO_ASM_ARM_SYSTEM_H */

// vim: ts=4 et sw=4 sts=4
