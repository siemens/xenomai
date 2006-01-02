/**
 *   @ingroup hal
 *   @file
 *
 *   Generic Real-Time HAL.
 *   Copyright &copy; 2005 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * @addtogroup hal
 *@{*/

#ifndef _XENO_ASM_GENERIC_HAL_H
#define _XENO_ASM_GENERIC_HAL_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/init.h>
#include <asm/xenomai/wrappers.h>

#define RTHAL_DOMAIN_ID		0x58454e4f

#define RTHAL_TIMER_FREQ	(rthal_tunables.timer_freq)
#define RTHAL_CPU_FREQ		(rthal_tunables.cpu_freq)
#define RTHAL_NR_APCS		BITS_PER_LONG

#define RTHAL_EVENT_PROPAGATE   0
#define RTHAL_EVENT_STOP        1

#ifdef CONFIG_ADEOS_CORE
#error "Adeos oldgen support has been deprecated; please upgrade to the I-pipe series."
#error "See http://download.gna.org/adeos/patches/"
#endif /* CONFIG_ADEOS_CORE */

#ifndef CONFIG_IPIPE
#error "Adeos kernel support is required to run this software."
#error "See http://download.gna.org/adeos/patches/"
#endif /* !CONFIG_IPIPE */

#define RTHAL_NR_CPUS		IPIPE_NR_CPUS
#define RTHAL_ROOT_PRIO		IPIPE_ROOT_PRIO
#define RTHAL_NR_FAULTS		IPIPE_NR_FAULTS

#define RTHAL_SERVICE_IPI0	IPIPE_SERVICE_IPI0
#define RTHAL_SERVICE_VECTOR0	IPIPE_SERVICE_VECTOR0
#define RTHAL_SERVICE_IPI1	IPIPE_SERVICE_IPI1
#define RTHAL_SERVICE_VECTOR1	IPIPE_SERVICE_VECTOR1
#define RTHAL_SERVICE_IPI2	IPIPE_SERVICE_IPI2
#define RTHAL_SERVICE_VECTOR2	IPIPE_SERVICE_VECTOR2
#define RTHAL_SERVICE_IPI3	IPIPE_SERVICE_IPI3
#define RTHAL_SERVICE_VECTOR3	IPIPE_SERVICE_VECTOR3
#define RTHAL_CRITICAL_IPI	IPIPE_CRITICAL_IPI

typedef struct ipipe_domain rthal_pipeline_stage_t;

#ifdef IPIPE_RW_LOCK_UNLOCKED
typedef ipipe_spinlock_t rthal_spinlock_t;
#define RTHAL_SPIN_LOCK_UNLOCKED IPIPE_SPIN_LOCK_UNLOCKED
typedef ipipe_rwlock_t rthal_rwlock_t;
#define RTHAL_RW_LOCK_UNLOCKED   IPIPE_RW_LOCK_UNLOCKED
#else /* !IPIPE_RW_LOCK_UNLOCKED */
#ifdef RAW_RW_LOCK_UNLOCKED
typedef raw_spinlock_t rthal_spinlock_t;
#define RTHAL_SPIN_LOCK_UNLOCKED RAW_SPIN_LOCK_UNLOCKED
typedef raw_rwlock_t rthal_rwlock_t;
#define RTHAL_RW_LOCK_UNLOCKED RAW_RW_LOCK_UNLOCKED
#else /* !RAW_RW_LOCK_UNLOCKED */
typedef spinlock_t rthal_spinlock_t;
#define RTHAL_SPIN_LOCK_UNLOCKED SPIN_LOCK_UNLOCKED
typedef rwlock_t rthal_rwlock_t;
#define RTHAL_RW_LOCK_UNLOCKED RW_LOCK_UNLOCKED
#endif /* RAW_RW_LOCK_UNLOCKED */
#endif /* IPIPE_RW_LOCK_UNLOCKED */

#define rthal_local_irq_disable()	ipipe_stall_pipeline_from(&rthal_domain)
#define rthal_local_irq_enable()	ipipe_unstall_pipeline_from(&rthal_domain)
#define rthal_local_irq_save(x)		((x) = !!ipipe_test_and_stall_pipeline_from(&rthal_domain))
#define rthal_local_irq_restore(x)	ipipe_restore_pipeline_from(&rthal_domain,(x))
#define rthal_local_irq_flags(x)	((x) = !!ipipe_test_pipeline_from(&rthal_domain))
#define rthal_local_irq_test()		(!!ipipe_test_pipeline_from(&rthal_domain))
#define rthal_local_irq_sync(x)		((x) = !!ipipe_test_and_unstall_pipeline_from(&rthal_domain))
#define rthal_stage_irq_enable(dom)	ipipe_unstall_pipeline_from(dom)
#define rthal_local_irq_save_hw(x)	local_irq_save_hw(x)
#define rthal_local_irq_restore_hw(x)	local_irq_restore_hw(x)
#define rthal_local_irq_enable_hw()	local_irq_enable_hw()
#define rthal_local_irq_disable_hw()	local_irq_disable_hw()
#define rthal_local_irq_flags_hw(x)	local_save_flags_hw(x)

#define rthal_write_lock(lock)		write_lock_hw(lock)
#define rthal_write_unlock(lock)	write_unlock_hw(lock)
#define rthal_read_lock(lock)		read_lock_hw(lock)
#define rthal_read_unlock(lock)		read_unlock_hw(lock)
#define rthal_spin_lock(lock)		spin_lock_hw(lock)
#define rthal_spin_unlock(lock)		spin_unlock_hw(lock)

#define rthal_root_domain			ipipe_root_domain
#define rthal_current_domain			ipipe_current_domain
#define rthal_declare_cpuid			ipipe_declare_cpuid

#define rthal_load_cpuid()			ipipe_load_cpuid()
#define rthal_suspend_domain()			ipipe_suspend_domain()
#define rthal_grab_superlock(syncfn)		ipipe_critical_enter(syncfn)
#define rthal_release_superlock(x)		ipipe_critical_exit(x)
#define rthal_propagate_irq(irq)		ipipe_propagate_irq(irq)
#define rthal_set_irq_affinity(irq,aff)		ipipe_set_irq_affinity(irq,aff)
#define rthal_schedule_irq(irq)			ipipe_schedule_irq(irq)
#define rthal_virtualize_irq(dom,irq,isr,ackfn,mode) ipipe_virtualize_irq(dom,irq,isr,ackfn,mode)
#define rthal_alloc_virq()			ipipe_alloc_virq()
#define rthal_free_virq(irq)			ipipe_free_virq(irq)
#define rthal_trigger_irq(irq)			ipipe_trigger_irq(irq)
#define rthal_get_sysinfo(ibuf)			ipipe_get_sysinfo(ibuf)
#define rthal_alloc_ptdkey()			ipipe_alloc_ptdkey()
#define rthal_free_ptdkey(key)			ipipe_free_ptdkey(key)
#define rthal_send_ipi(irq,cpus)		ipipe_send_ipi(irq,cpus)
#define rthal_lock_irq(dom,cpu,irq)		__ipipe_lock_irq(dom,cpu,irq)
#define rthal_unlock_irq(dom,irq)		__ipipe_unlock_irq(dom,irq)
#ifdef IPIPE_GRAB_TIMER		/* FIXME: should we need this with the I-pipe? */
#define rthal_set_timer(ns)			ipipe_tune_timer(ns,ns ? 0 : IPIPE_GRAB_TIMER)
#else /* !IPIPE_GRAB_TIMER */
#define rthal_set_timer(ns)			ipipe_tune_timer(ns,0)
#endif /* IPIPE_GRAB_TIMER */
#define rthal_reset_timer()			ipipe_tune_timer(0,IPIPE_RESET_TIMER)

#define rthal_lock_cpu(x)			ipipe_lock_cpu(x)
#define rthal_unlock_cpu(x)			ipipe_unlock_cpu(x)
#define rthal_get_cpu(x)			ipipe_get_cpu(x)
#define rthal_put_cpu(x)			ipipe_put_cpu(x)
#define rthal_processor_id()			ipipe_processor_id()

#define rthal_setsched_root(t,pol,prio)		ipipe_setscheduler_root(t,pol,prio)
#define rthal_reenter_root(t,pol,prio)		ipipe_reenter_root(t,pol,prio)
#define rthal_emergency_console()		ipipe_set_printk_sync(ipipe_current_domain)
#define rthal_read_tsc(v)			ipipe_read_tsc(v)

static inline unsigned long rthal_get_cpufreq(void)
{
    struct ipipe_sysinfo sysinfo;
    rthal_get_sysinfo(&sysinfo);
    return (unsigned long)sysinfo.cpufreq;
}

#define RTHAL_DECLARE_EVENT(hdlr) \
static int hdlr (unsigned event, struct ipipe_domain *ipd, void *data) \
{ \
    return do_##hdlr(event,ipd->domid,data); \
}

#define RTHAL_DECLARE_SCHEDULE_EVENT(hdlr) \
static int hdlr (unsigned event, struct ipipe_domain *ipd, void *data) \
{ \
    struct task_struct *p = (struct task_struct *)data; \
    do_##hdlr(p);					\
    return RTHAL_EVENT_PROPAGATE; \
}

#define RTHAL_DECLARE_SETSCHED_EVENT(hdlr) \
static int hdlr (unsigned event, struct ipipe_domain *ipd, void *data) \
{ \
    struct task_struct *p = (struct task_struct *)data; \
    do_##hdlr(p,p->rt_priority);			\
    return RTHAL_EVENT_PROPAGATE; \
}

#define RTHAL_DECLARE_SIGWAKE_EVENT(hdlr) \
static int hdlr (unsigned event, struct ipipe_domain *ipd, void *data) \
{ \
    struct task_struct *p = (struct task_struct *)data; \
    do_##hdlr(p);					\
    return RTHAL_EVENT_PROPAGATE; \
}

#define RTHAL_DECLARE_EXIT_EVENT(hdlr) \
static int hdlr (unsigned event, struct ipipe_domain *ipd, void *data) \
{ \
    struct task_struct *p = (struct task_struct *)data; \
    do_##hdlr(p);					\
    return RTHAL_EVENT_PROPAGATE; \
}

#define rthal_catch_taskexit(hdlr)	\
    ipipe_catch_event(ipipe_root_domain,IPIPE_EVENT_EXIT,hdlr)
#define rthal_catch_sigwake(hdlr)	\
    ipipe_catch_event(ipipe_root_domain,IPIPE_EVENT_SIGWAKE,hdlr)
#define rthal_catch_schedule(hdlr)	\
    ipipe_catch_event(ipipe_root_domain,IPIPE_EVENT_SCHEDULE,hdlr)
#define rthal_catch_setsched(hdlr)	\
    ipipe_catch_event(&rthal_domain,IPIPE_EVENT_SETSCHED,hdlr)
#define rthal_catch_losyscall(hdlr)	\
    ipipe_catch_event(ipipe_root_domain,IPIPE_EVENT_SYSCALL,hdlr)
#define rthal_catch_hisyscall(hdlr)	\
    ipipe_catch_event(&rthal_domain,IPIPE_EVENT_SYSCALL,hdlr)
#define rthal_catch_exception(ex,hdlr)	\
    ipipe_catch_event(&rthal_domain,ex,hdlr)

#define rthal_register_domain(_dom,_name,_id,_prio,_entry)	\
 ({	\
    struct ipipe_domain_attr attr; \
    ipipe_init_attr(&attr); \
    attr.name = _name;	    \
    attr.entry = _entry;   \
    attr.domid = _id;	    \
    attr.priority = _prio; \
    ipipe_register_domain(_dom,&attr); \
 })

#define rthal_unregister_domain(dom)	ipipe_unregister_domain(dom)

#define RTHAL_DECLARE_DOMAIN(entry)	\
void entry (void)	\
{				\
    do_##entry();		\
}

extern void rthal_domain_entry(void);

#define rthal_spin_lock_irq(lock) \
do {  \
    rthal_local_irq_disable(); \
    rthal_spin_lock(lock); \
} while(0)

#define rthal_spin_unlock_irq(lock) \
do {  \
    rthal_spin_unlock(lock); \
    rthal_local_irq_enable(); \
} while(0)

#define rthal_spin_lock_irqsave(lock,x) \
do {  \
    rthal_local_irq_save(x); \
    rthal_spin_lock(lock); \
} while(0)

#define rthal_spin_unlock_irqrestore(lock,x) \
do {  \
    rthal_spin_unlock(lock); \
    rthal_local_irq_restore(x); \
} while(0)

#define rthal_printk	printk

typedef void (*rthal_irq_handler_t)(unsigned irq,
				    void *cookie);

struct rthal_calibration_data {

    unsigned long cpu_freq;
    unsigned long timer_freq;
};

typedef int (*rthal_trap_handler_t)(unsigned trapno,
				    unsigned domid,
				    void *data);

extern unsigned long rthal_cpufreq_arg;

extern unsigned long rthal_timerfreq_arg;

extern rthal_pipeline_stage_t rthal_domain;

extern struct rthal_calibration_data rthal_tunables;

extern volatile int rthal_sync_op;

extern volatile unsigned long rthal_cpu_realtime;

extern rthal_trap_handler_t rthal_trap_handler;

extern int rthal_realtime_faults[RTHAL_NR_CPUS][RTHAL_NR_FAULTS];

extern int rthal_arch_init(void);

extern void rthal_arch_cleanup(void);

    /* Private interface -- Internal use only */

unsigned long rthal_critical_enter(void (*synch)(void));

void rthal_critical_exit(unsigned long flags);

    /* Public interface */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int rthal_init(void);

void rthal_exit(void);

int rthal_irq_request(unsigned irq,
		      void (*handler)(unsigned irq, void *cookie),
		      int (*ackfn)(unsigned irq),
		      void *cookie);

int rthal_irq_release(unsigned irq);

int rthal_irq_enable(unsigned irq);

int rthal_irq_disable(unsigned irq);

int rthal_irq_host_request(unsigned irq,
			   irqreturn_t (*handler)(int irq,
						  void *dev_id,
						  struct pt_regs *regs), 
			   char *name,
			   void *dev_id);

int rthal_irq_host_release(unsigned irq,
			   void *dev_id);

int rthal_irq_host_pend(unsigned irq);

int rthal_apc_alloc(const char *name,
		    void (*handler)(void *cookie),
		    void *cookie);

int rthal_apc_free(int apc);

int rthal_apc_schedule(int apc);

int rthal_irq_affinity(unsigned irq,
		       cpumask_t cpumask,
		       cpumask_t *oldmask);

int rthal_timer_request(void (*handler)(void),
			unsigned long nstick);

void rthal_timer_release(void);

rthal_trap_handler_t rthal_trap_catch(rthal_trap_handler_t handler);

unsigned long rthal_timer_calibrate(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

/*@}*/

#endif /* !_XENO_ASM_GENERIC_HAL_H */
