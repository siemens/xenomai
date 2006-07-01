/*
 * Copyright (C) 2001,2002,2003,2004,2005 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004,2005 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

#ifndef _XENO_ASM_GENERIC_SYSTEM_H
#define _XENO_ASM_GENERIC_SYSTEM_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <asm/mmu_context.h>
#include <asm/ptrace.h>
#include <linux/config.h>
#include <asm/xenomai/hal.h>
#include <asm/xenomai/atomic.h>
#include <nucleus/shadow.h>

/* Tracer interface */
#define xnarch_trace_max_begin(v)		rthal_trace_max_begin(v)
#define xnarch_trace_max_end(v)		rthal_trace_max_end(v)
#define xnarch_trace_max_reset()		rthal_trace_max_reset()
#define xnarch_trace_user_start()		rthal_trace_user_start()
#define xnarch_trace_user_stop(v)		rthal_trace_user_stop(v)
#define xnarch_trace_user_freeze(v, once) 	rthal_trace_user_freeze(v, once)
#define xnarch_trace_special(id, v)		rthal_trace_special(id, v)
#define xnarch_trace_special_u64(id, v)	rthal_trace_special_u64(id, v)
#define xnarch_trace_pid(pid, prio)		rthal_trace_pid(pid, prio)
#define xnarch_trace_panic_freeze()		rthal_trace_panic_freeze()
#define xnarch_trace_panic_dump()		rthal_trace_panic_dump()

#ifndef xnarch_fault_um
#define xnarch_fault_um(fi) user_mode(fi->regs)
#endif

#define module_param_value(parm) (parm)

typedef unsigned long spl_t;

#define splhigh(x)  rthal_local_irq_save(x)
#ifdef CONFIG_SMP
#define splexit(x)  rthal_local_irq_restore((x) & 1)
#else /* !CONFIG_SMP */
#define splexit(x)  rthal_local_irq_restore(x)
#endif /* CONFIG_SMP */
#define splnone()   rthal_local_irq_enable()
#define spltest()   rthal_local_irq_test()
#define splget(x)   rthal_local_irq_flags(x)

#if defined(CONFIG_SMP) && \
    (defined(CONFIG_XENO_OPT_STATS) || defined(CONFIG_XENO_OPT_DEBUG))

typedef struct {

        unsigned long long spin_time;
        unsigned long long lock_time;
        const char *file;
        const char *function;
        unsigned line;

} xnlockinfo_t;

typedef struct {

    volatile unsigned long lock;
    const char *file;
    const char *function;
    unsigned line;
    int cpu;
    unsigned long long spin_time;
    unsigned long long lock_date;

} xnlock_t;

#define XNARCH_LOCK_UNLOCKED (xnlock_t) {       \
        0,                                      \
        NULL,                                   \
        NULL,                                   \
        0,                                      \
        -1,                                     \
        0LL,                                    \
        0LL,                                    \
}

#define CONFIG_XENO_SPINLOCK_DEBUG  1

#else /* !(CONFIG_XENO_OPT_STATS && CONFIG_SMP) */

typedef struct { volatile unsigned long lock; } xnlock_t;

#define XNARCH_LOCK_UNLOCKED (xnlock_t) { 0 }

#endif /* CONFIG_XENO_OPT_STATS && CONFIG_SMP */

#define XNARCH_NR_CPUS               RTHAL_NR_CPUS

#define XNARCH_TIMER_IRQ	     RTHAL_TIMER_IRQ

#define XNARCH_ROOT_STACKSZ   0	/* Only a placeholder -- no stack */

#define XNARCH_PROMPT "Xenomai: "
#define xnarch_loginfo(fmt,args...)  printk(KERN_INFO XNARCH_PROMPT fmt , ##args)
#define xnarch_logwarn(fmt,args...)  printk(KERN_WARNING XNARCH_PROMPT fmt , ##args)
#define xnarch_logerr(fmt,args...)   printk(KERN_ERR XNARCH_PROMPT fmt , ##args)
#define xnarch_printf(fmt,args...)   printk(KERN_INFO XNARCH_PROMPT fmt , ##args)

#define xnarch_ullmod(ull,uld,rem)   ({ xnarch_ulldiv(ull,uld,rem); (*rem); })
#define xnarch_uldiv(ull, d)         rthal_uldivrem(ull, d, NULL)
#define xnarch_ulmod(ull, d)         ({ u_long _rem;                    \
                                        rthal_uldivrem(ull,d,&_rem); _rem; })

#define xnarch_ullmul                rthal_ullmul
#define xnarch_uldivrem              rthal_uldivrem
#define xnarch_ulldiv                rthal_ulldiv
#define xnarch_imuldiv               rthal_imuldiv
#define xnarch_llimd                 rthal_llimd
#define xnarch_get_cpu_tsc           rthal_rdtsc

typedef cpumask_t xnarch_cpumask_t;

#ifdef CONFIG_SMP
#define xnarch_cpu_online_map            cpu_online_map
#else
#define xnarch_cpu_online_map		 cpumask_of_cpu(0)
#endif
#define xnarch_num_online_cpus()          num_online_cpus()
#define xnarch_cpu_set(cpu, mask)         cpu_set(cpu, mask)
#define xnarch_cpu_clear(cpu, mask)       cpu_clear(cpu, mask)
#define xnarch_cpus_clear(mask)           cpus_clear(mask)
#define xnarch_cpu_isset(cpu, mask)       cpu_isset(cpu, mask)
#define xnarch_cpus_and(dst, src1, src2)  cpus_and(dst, src1, src2)
#define xnarch_cpus_equal(mask1, mask2)   cpus_equal(mask1, mask2)
#define xnarch_cpus_empty(mask)           cpus_empty(mask)
#define xnarch_cpumask_of_cpu(cpu)        cpumask_of_cpu(cpu)
#define xnarch_cpu_test_and_set(cpu,mask) cpu_test_and_set(cpu,mask)

#define xnarch_first_cpu(mask)            first_cpu(mask)
#define XNARCH_CPU_MASK_ALL               CPU_MASK_ALL

typedef struct xnarch_heapcb {

    atomic_t numaps;	/* # of active user-space mappings. */

    int kmflags;	/* Kernel memory flags (0 if vmalloc()). */

    void *heapbase;	/* Shared heap memory base. */

} xnarch_heapcb_t;

#ifdef __cplusplus
extern "C" {
#endif

static inline long long xnarch_tsc_to_ns (long long ts)
{
    return xnarch_llimd(ts,1000000000,RTHAL_CPU_FREQ);
}

static inline long long xnarch_ns_to_tsc (long long ns)
{
    return xnarch_llimd(ns,RTHAL_CPU_FREQ,1000000000);
}

static inline unsigned long long xnarch_get_cpu_time (void)
{
    return xnarch_tsc_to_ns(xnarch_get_cpu_tsc());
}

static inline unsigned long long xnarch_get_cpu_freq (void)
{
    return RTHAL_CPU_FREQ;
}

static inline unsigned xnarch_current_cpu (void)
{
    return rthal_processor_id();
}

#define xnarch_declare_cpuid  rthal_declare_cpuid
#define xnarch_get_cpu(flags) rthal_get_cpu(flags)
#define xnarch_put_cpu(flags) rthal_put_cpu(flags)

#define xnarch_halt(emsg) \
do { \
    rthal_emergency_console(); \
    xnarch_logerr("fatal: %s\n",emsg); \
    show_stack(NULL,NULL);			\
    xnarch_trace_panic_dump();			\
    for (;;) cpu_relax();			\
} while(0)

static inline int xnarch_setimask (int imask)
{
    spl_t s;
    splhigh(s);
    splexit(!!imask);
    return !!s;
}

#ifdef CONFIG_SMP

#ifdef CONFIG_XENO_SPINLOCK_DEBUG
#define xnlock_get(lock) \
    __xnlock_get(lock, __FILE__, __LINE__,__FUNCTION__)
#define xnlock_get_irqsave(lock,x) \
    ((x) = __xnlock_get_irqsave(lock, __FILE__, __LINE__,__FUNCTION__))
#else /* !CONFIG_XENO_SPINLOCK_DEBUG */
#define xnlock_get(lock)            __xnlock_get(lock)
#define xnlock_get_irqsave(lock,x)  ((x) = __xnlock_get_irqsave(lock))
#endif /* CONFIG_XENO_SPINLOCK_DEBUG */
#define xnlock_clear_irqoff(lock)   xnlock_put_irqrestore(lock,1)
#define xnlock_clear_irqon(lock)    xnlock_put_irqrestore(lock,0)

static inline void xnlock_init (xnlock_t *lock)
{
    *lock = XNARCH_LOCK_UNLOCKED;
}

#ifdef CONFIG_XENO_SPINLOCK_DEBUG

#define XNARCH_DEBUG_SPIN_LIMIT 3000000

static inline void __xnlock_get (xnlock_t *lock,
				 const char *file,
				 unsigned line,
				 const char *function)
{
    unsigned spin_count = 0;
#else /* !CONFIG_XENO_SPINLOCK_DEBUG */
static inline void __xnlock_get (xnlock_t *lock)
{
#endif /* CONFIG_XENO_SPINLOCK_DEBUG */
    rthal_declare_cpuid;

    rthal_load_cpuid();

    if (!test_and_set_bit(cpuid,&lock->lock))
        {
#ifdef CONFIG_XENO_SPINLOCK_DEBUG
        unsigned long long lock_date = rthal_rdtsc();
#endif /* CONFIG_XENO_SPINLOCK_DEBUG */
        while (test_and_set_bit(BITS_PER_LONG - 1,&lock->lock))
            /* Use an non-locking test in the inner loop, as Linux'es
               bit_spin_lock. */
            while (test_bit(BITS_PER_LONG - 1,&lock->lock))
                {
                cpu_relax();

#ifdef CONFIG_XENO_SPINLOCK_DEBUG
                if (++spin_count == XNARCH_DEBUG_SPIN_LIMIT)
                    {
                    rthal_emergency_console();
                    printk(KERN_ERR
                           "Xenomai: stuck on nucleus lock %p\n"
                           "       waiter = %s:%u (%s(), CPU #%d)\n"
                           "       owner  = %s:%u (%s(), CPU #%d)\n",
                           lock,file,line,function,cpuid,
                           lock->file,lock->line,lock->function,lock->cpu);
                    show_stack(NULL,NULL);
                    for (;;)
                        cpu_relax();
                    }
#endif /* CONFIG_XENO_SPINLOCK_DEBUG */
                }

#ifdef CONFIG_XENO_SPINLOCK_DEBUG
        lock->spin_time = rthal_rdtsc() - lock_date;
        lock->lock_date = lock_date;
        lock->file = file;
        lock->function = function;
        lock->line = line;
        lock->cpu = cpuid;
#endif /* CONFIG_XENO_SPINLOCK_DEBUG */
        }
}

static inline void xnlock_put (xnlock_t *lock)
{
    rthal_declare_cpuid;

    rthal_load_cpuid();
    if (test_and_clear_bit(cpuid,&lock->lock))
	{
#ifdef CONFIG_XENO_OPT_STATS
	extern xnlockinfo_t xnlock_stats[];

	unsigned long long lock_time = rthal_rdtsc() - lock->lock_date;

	if (lock_time > xnlock_stats[cpuid].lock_time)
	    {
	    xnlock_stats[cpuid].lock_time = lock_time;
	    xnlock_stats[cpuid].spin_time = lock->spin_time;
	    xnlock_stats[cpuid].file = lock->file;
	    xnlock_stats[cpuid].function = lock->function;
	    xnlock_stats[cpuid].line = lock->line;
	    }
#endif /* CONFIG_XENO_OPT_STATS */

	clear_bit(BITS_PER_LONG - 1,&lock->lock);
	}
#ifdef CONFIG_XENO_SPINLOCK_DEBUG
    else
	{
	rthal_emergency_console();
	printk(KERN_ERR
	       "Xenomai: unlocking unlocked nucleus lock %p\n"
	       "       owner  = %s:%u (%s(), CPU #%d)\n",
	       lock,lock->file,lock->line,lock->function,lock->cpu);
	show_stack(NULL,NULL);
	for (;;)
	    cpu_relax();
	}
#endif
}

#ifdef CONFIG_XENO_SPINLOCK_DEBUG

static inline spl_t __xnlock_get_irqsave (xnlock_t *lock,
                                          const char *file,
                                          unsigned line,
                                          const char *function)
{
    unsigned spin_count = 0;
#else /* !CONFIG_XENO_SPINLOCK_DEBUG */
static inline spl_t __xnlock_get_irqsave (xnlock_t *lock)
{
#endif /* CONFIG_XENO_SPINLOCK_DEBUG */
    rthal_declare_cpuid;
    unsigned long flags;

    rthal_local_irq_save(flags);

    rthal_load_cpuid();

    if (!test_and_set_bit(cpuid,&lock->lock))
        {
#ifdef CONFIG_XENO_SPINLOCK_DEBUG
        unsigned long long lock_date = rthal_rdtsc();
#endif /* CONFIG_XENO_SPINLOCK_DEBUG */
        while (test_and_set_bit(BITS_PER_LONG - 1,&lock->lock))
            /* Use an non-locking test in the inner loop, as Linux'es
               bit_spin_lock. */
            while (test_bit(BITS_PER_LONG - 1,&lock->lock))
                {
                cpu_relax();

#ifdef CONFIG_XENO_SPINLOCK_DEBUG
                if (++spin_count == XNARCH_DEBUG_SPIN_LIMIT)
                    {
                    rthal_emergency_console();
                    printk(KERN_ERR
                           "Xenomai: stuck on nucleus lock %p\n"
                           "       waiter = %s:%u (%s(), CPU #%d)\n"
                           "       owner  = %s:%u (%s(), CPU #%d)\n",
                           lock,file,line,function,cpuid,
                           lock->file,lock->line,lock->function,lock->cpu);
                    show_stack(NULL,NULL);
                    for (;;)
                        cpu_relax();
                    }
#endif /* CONFIG_XENO_SPINLOCK_DEBUG */
                }

#ifdef CONFIG_XENO_SPINLOCK_DEBUG
        lock->spin_time = rthal_rdtsc() - lock_date;
        lock->lock_date = lock_date;
        lock->file = file;
        lock->function = function;
        lock->line = line;
        lock->cpu = cpuid;
#endif /* CONFIG_XENO_SPINLOCK_DEBUG */
        }
    else
        flags |= 2;

    return flags;
}

static inline void xnlock_put_irqrestore (xnlock_t *lock, spl_t flags)
{
    if (!(flags & 2))
        {
        rthal_declare_cpuid;

        rthal_local_irq_disable();

        rthal_load_cpuid();
        if (test_and_clear_bit(cpuid,&lock->lock))
	    {
#ifdef CONFIG_XENO_OPT_STATS
	    extern xnlockinfo_t xnlock_stats[];

            unsigned long long lock_time = rthal_rdtsc() - lock->lock_date;

            if (lock_time > xnlock_stats[cpuid].lock_time)
                {
                xnlock_stats[cpuid].lock_time = lock_time;
                xnlock_stats[cpuid].spin_time = lock->spin_time;
                xnlock_stats[cpuid].file = lock->file;
                xnlock_stats[cpuid].function = lock->function;
                xnlock_stats[cpuid].line = lock->line;
                }
#endif /* CONFIG_XENO_OPT_STATS */

            clear_bit(BITS_PER_LONG - 1,&lock->lock);
	    }
#ifdef CONFIG_XENO_SPINLOCK_DEBUG
        else
            {
            rthal_emergency_console();
            printk(KERN_ERR
                   "Xenomai: unlocking unlocked nucleus lock %p\n"
                   "       owner  = %s:%u (%s(), CPU #%d)\n",
                   lock,lock->file,lock->line,lock->function,lock->cpu);
            show_stack(NULL,NULL);
            for (;;)
                cpu_relax();
            }
#endif
        }

    rthal_local_irq_restore(flags & 1);
}

#else /* !CONFIG_SMP */

#define xnlock_init(lock)              do { } while(0)
#define xnlock_get(lock)               do { } while(0)
#define xnlock_put(lock)               do { } while(0)
#define xnlock_get_irqsave(lock,x)     rthal_local_irq_save(x)
#define xnlock_put_irqrestore(lock,x)  rthal_local_irq_restore(x)
#define xnlock_clear_irqoff(lock)      rthal_local_irq_disable()
#define xnlock_clear_irqon(lock)       rthal_local_irq_enable()

#endif /* CONFIG_SMP */

static inline int xnarch_remap_vm_page(struct vm_area_struct *vma,
				       unsigned long from,
				       unsigned long to)
{
    return wrap_remap_vm_page(vma,from,to);
}

static inline int xnarch_remap_io_page_range(struct vm_area_struct *vma,
					     unsigned long from,
					     unsigned long to,
					     unsigned long size,
					     pgprot_t prot)
{
    return wrap_remap_io_page_range(vma,from,to,size,prot);
}

#ifdef XENO_POD_MODULE

#ifdef CONFIG_SMP

static inline int xnarch_send_ipi (xnarch_cpumask_t cpumask)
{
    return rthal_send_ipi(RTHAL_SERVICE_IPI0, cpumask);
}

static inline int xnarch_hook_ipi (void (*handler)(void))
{
    return rthal_virtualize_irq(&rthal_domain,
				RTHAL_SERVICE_IPI0,
				(rthal_irq_handler_t) handler,
				NULL,
				NULL,
				IPIPE_HANDLE_MASK | IPIPE_WIRED_MASK);
}

static inline int xnarch_release_ipi (void)
{
    return rthal_virtualize_irq(&rthal_domain,
				RTHAL_SERVICE_IPI0,
				NULL,
				NULL,
				NULL,
				IPIPE_PASS_MASK);
}

static struct linux_semaphore xnarch_finalize_sync;

static void xnarch_finalize_cpu(unsigned irq)
{
    up(&xnarch_finalize_sync);
}

static inline void xnarch_notify_halt(void)
{
    xnarch_cpumask_t other_cpus = cpu_online_map;
    unsigned cpu, nr_cpus = num_online_cpus();
    unsigned long flags;
    rthal_declare_cpuid;

    sema_init(&xnarch_finalize_sync,0);

    /* Here rthal_current_domain is in fact root, since xnarch_notify_halt is
       called from xnpod_shutdown, itself called from Linux
       context. */

    rthal_virtualize_irq(rthal_current_domain,
			 RTHAL_SERVICE_IPI2,
			 (rthal_irq_handler_t)xnarch_finalize_cpu,
			 NULL,
			 NULL,
			 IPIPE_HANDLE_MASK);

    rthal_lock_cpu(flags);
    cpu_clear(cpuid, other_cpus);
    rthal_send_ipi(RTHAL_SERVICE_IPI2, other_cpus);
    rthal_unlock_cpu(flags);

    for(cpu=0; cpu < nr_cpus-1; ++cpu)
        down(&xnarch_finalize_sync);
    
    rthal_virtualize_irq(rthal_current_domain,
			 RTHAL_SERVICE_IPI2,
			 NULL,
			 NULL,
			 NULL,
			 IPIPE_PASS_MASK);

    rthal_release_control();
}

#else /* !CONFIG_SMP */

static inline int xnarch_send_ipi (xnarch_cpumask_t cpumask)
{
    return 0;
}

static inline int xnarch_hook_ipi (void (*handler)(void))
{
    return 0;
}

static inline int xnarch_release_ipi (void)
{
    return 0;
}

#define xnarch_notify_halt()  rthal_release_control()

#endif /* CONFIG_SMP */

static inline void xnarch_notify_shutdown(void)
{
#ifdef CONFIG_SMP
    /* The HAL layer also sets the same CPU affinity so that both
       modules keep their execution sequence on SMP boxen. */
    set_cpus_allowed(current,cpumask_of_cpu(0));
#endif /* CONFIG_SMP */
#ifdef CONFIG_XENO_OPT_PERVASIVE
    xnshadow_release_events();
#endif /* CONFIG_XENO_OPT_PERVASIVE */
    /* Wait for the currently processed events to drain. */
    set_current_state(TASK_UNINTERRUPTIBLE);
    schedule_timeout(50);
    xnarch_release_ipi();
}

static void xnarch_notify_ready (void)
{
    rthal_grab_control();
#ifdef CONFIG_XENO_OPT_PERVASIVE    
    xnshadow_grab_events();
#endif /* CONFIG_XENO_OPT_PERVASIVE */
}

static inline unsigned long long xnarch_get_sys_time(void)
{
    struct timeval tv;
    do_gettimeofday(&tv);
    return tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000;
}

#endif /* XENO_POD_MODULE */

#ifdef XENO_INTR_MODULE

static inline int xnarch_hook_irq (unsigned irq,
				   rthal_irq_handler_t handler,
				   rthal_irq_ackfn_t ackfn,
				   void *cookie)
{
    return rthal_irq_request(irq,handler,ackfn,cookie);
}

static inline int xnarch_release_irq (unsigned irq)
{
    return rthal_irq_release(irq);
}

static inline int xnarch_enable_irq (unsigned irq)
{
    return rthal_irq_enable(irq);
}

static inline int xnarch_disable_irq (unsigned irq)
{
    return rthal_irq_disable(irq);
}

static inline int xnarch_end_irq (unsigned irq)
{
     return rthal_irq_end(irq);
}
                                                                                
static inline void xnarch_chain_irq (unsigned irq)
{
    rthal_irq_host_pend(irq);
}

static inline xnarch_cpumask_t xnarch_set_irq_affinity (unsigned irq,
							xnarch_cpumask_t affinity)
{
    return rthal_set_irq_affinity(irq,affinity);
}

#endif /* XENO_INTR_MODULE */

#ifdef XENO_HEAP_MODULE

static inline void xnarch_init_heapcb (xnarch_heapcb_t *hcb)

{
    atomic_set(&hcb->numaps,0);
    hcb->kmflags = 0;
    hcb->heapbase = NULL;
}

#endif /* XENO_HEAP_MODULE */

#ifdef __cplusplus
}
#endif

/* Dashboard and graph control. */
#define XNARCH_DECL_DISPLAY_CONTEXT();
#define xnarch_init_display_context(obj)
#define xnarch_create_display(obj,name,tag)
#define xnarch_delete_display(obj)
#define xnarch_post_graph(obj,state)
#define xnarch_post_graph_if(obj,state,cond)

#endif /* !_XENO_ASM_GENERIC_SYSTEM_H */
