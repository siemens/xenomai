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

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/init.h>
#include <linux/clockchips.h>
#include <asm/byteorder.h>
#include <asm/xenomai/wrappers.h>
#include <asm/xenomai/arith.h>

struct rthal_archdata {
	struct ipipe_domain domain;
	unsigned long timer_freq;
	unsigned long clock_freq;
	volatile int sync_op;
	unsigned int apc_virq;
	unsigned long apc_map;
	unsigned long apc_pending[NR_CPUS];
	struct {
		void (*handler)(void *cookie);
		void *cookie;
		const char *name;
		unsigned long hits[NR_CPUS];
	} apc_table[BITS_PER_LONG];
	ipipe_event_handler_t trap_handler;
	unsigned int faults[NR_CPUS][IPIPE_NR_FAULTS];
#ifdef CONFIG_SMP
	cpumask_t supported_cpus;
#endif
};

#define RTHAL_TIMER_FREQ	(rthal_archdata.timer_freq)
#define RTHAL_CLOCK_FREQ	(rthal_archdata.clock_freq)

enum rthal_ktimer_mode { /* <!> Must follow enum clock_event_mode */
	KTIMER_MODE_UNUSED = 0,
	KTIMER_MODE_SHUTDOWN,
	KTIMER_MODE_PERIODIC,
	KTIMER_MODE_ONESHOT,
};

#ifdef __IPIPE_FEATURE_PIC_MUTE
#define rthal_mute_pic()		ipipe_mute_pic()
#define rthal_unmute_pic()		ipipe_unmute_pic()
#else /* !__IPIPE_FEATURE_PIC_MUTE */
#define rthal_mute_pic()		do { } while(0)
#define rthal_unmute_pic()		do { } while(0)
#endif /* __IPIPE_FEATURE_PIC_MUTE */

static inline unsigned long rthal_get_timerfreq(void)
{
	struct ipipe_sysinfo sysinfo;
	ipipe_get_sysinfo(&sysinfo);
	return (unsigned long)sysinfo.sys_hrtimer_freq;
}

static inline unsigned long rthal_get_clockfreq(void)
{
	struct ipipe_sysinfo sysinfo;
	ipipe_get_sysinfo(&sysinfo);
	return (unsigned long)sysinfo.sys_hrclock_freq;
}

static inline struct mm_struct *rthal_get_active_mm(void)
{
#ifdef CONFIG_IPIPE_WANT_PREEMPTIBLE_SWITCH
	return per_cpu(ipipe_active_mm, ipipe_processor_id());
#else
	return current->active_mm;
#endif
}

int rthal_arch_init(void);

void rthal_arch_cleanup(void);

unsigned long rthal_critical_enter(void (*synch)(void));

void rthal_critical_exit(unsigned long flags);

extern struct rthal_archdata rthal_archdata;

extern unsigned long rthal_timerfreq_arg;

extern unsigned long rthal_clockfreq_arg;

int rthal_init(void);

void rthal_exit(void);

int rthal_irq_request(unsigned int irq,
		      ipipe_irq_handler_t handler,
		      ipipe_irq_ackfn_t ackfn,
		      void *cookie);

int rthal_irq_release(unsigned int irq);

int rthal_irq_enable(unsigned int irq);

int rthal_irq_disable(unsigned int irq);

int rthal_irq_end(unsigned int irq);

/**
 * @fn static inline void rthal_irq_host_pend(unsigned int irq)
 *
 * @brief Propagate an IRQ event to Linux.
 *
 * Causes the given IRQ to be propagated down to the Adeos pipeline to
 * the Linux kernel. This operation is typically used after the given
 * IRQ has been processed into the Xenomai domain by a real-time
 * interrupt handler (see rthal_irq_request()), in case such interrupt
 * must also be handled by the Linux kernel.
 *
 * @param irq The interrupt number to propagate.  This value is
 * architecture-dependent.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Xenomai domain context.
 */
static inline void rthal_irq_host_pend(unsigned int irq)
{
	__ipipe_propagate_irq(irq);
}

int rthal_apc_alloc(const char *name,
		    void (*handler)(void *cookie),
		    void *cookie);

void rthal_apc_free(int apc);

static inline void __rthal_apc_schedule(int apc)
{
	int cpu = ipipe_processor_id();
	if (!__test_and_set_bit(apc, &rthal_archdata.apc_pending[cpu]))
		__ipipe_schedule_irq_root(rthal_archdata.apc_virq);
}

/**
 * @fn static inline int rthal_apc_schedule(int apc)
 *
 * @brief Schedule an APC invocation.
 *
 * This service marks the APC as pending for the Linux domain, so that
 * its handler will be called as soon as possible, when the Linux
 * domain gets back in control.
 *
 * When posted from the Linux domain, the APC handler is fired as soon
 * as the interrupt mask is explicitly cleared by some kernel
 * code. When posted from the Xenomai domain, the APC handler is
 * fired as soon as the Linux domain is resumed, i.e. after Xenomai has
 * completed all its pending duties.
 *
 * @param apc The APC id. to schedule.
 *
 * This service can be called from:
 *
 * - Any domain context, albeit the usual calling place is from the
 * Xenomai domain.
 */
static inline void rthal_apc_schedule(int apc)
{
	unsigned long flags;

	flags = ipipe_test_and_stall_pipeline_head() & 1;
	__rthal_apc_schedule(apc);
	ipipe_restore_pipeline_head(flags);
}

ipipe_event_handler_t rthal_trap_catch(ipipe_event_handler_t handler);

unsigned long rthal_timer_calibrate(void);

enum clock_event_mode;
struct clock_event_device;

int rthal_timer_request(void (*tick_handler)(void),
			void (*mode_emul)(enum clock_event_mode mode,
					  struct clock_event_device *cdev),
			int (*tick_emul) (unsigned long delay,
					  struct clock_event_device *cdev),
			int cpu);

void rthal_timer_notify_switch(enum clock_event_mode mode,
			       struct clock_event_device *cdev);

void rthal_timer_release(int cpu);

#ifdef CONFIG_SMP
#define rthal_supported_cpus rthal_archdata.supported_cpus

static inline int rthal_cpu_supported(int cpu)
{
	return cpu_isset(cpu, rthal_archdata.supported_cpus);
}
#else  /* !CONFIG_SMP */
#define rthal_supported_cpus CPU_MASK_ALL

static inline int rthal_cpu_supported(int cpu)
{
	return 1;
}
#endif /* !CONFIG_SMP */

/*@}*/

#endif /* !_XENO_ASM_GENERIC_HAL_H */
