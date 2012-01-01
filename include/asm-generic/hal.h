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
#include <linux/ipipe.h>
#include <linux/clockchips.h>
#include <asm/byteorder.h>
#include <asm/xenomai/wrappers.h>
#include <asm/xenomai/arith.h>

struct rthal_archdata {
	struct ipipe_domain domain;
	unsigned long timer_freq;
	unsigned long clock_freq;
	unsigned int apc_virq;
	unsigned long apc_map;
	unsigned long apc_pending[NR_CPUS];
	unsigned int escalate_virq;
	struct {
		void (*handler)(void *cookie);
		void *cookie;
		const char *name;
		unsigned long hits[NR_CPUS];
	} apc_table[BITS_PER_LONG];
	unsigned int faults[NR_CPUS][IPIPE_NR_FAULTS];
#ifdef CONFIG_SMP
	cpumask_t supported_cpus;
#endif
#ifdef CONFIG_XENO_LEGACY_IPIPE
	struct task_struct *task_hijacked[NR_CPUS];
#endif
};

extern struct rthal_archdata rthal_archdata;

#define RTHAL_TIMER_FREQ	(rthal_archdata.timer_freq)
#define RTHAL_CLOCK_FREQ	(rthal_archdata.clock_freq)

#include <asm-generic/xenomai/ipipe/wrappers.h>

enum rthal_ktimer_mode { /* <!> Must follow enum clock_event_mode */
	KTIMER_MODE_UNUSED = 0,
	KTIMER_MODE_SHUTDOWN,
	KTIMER_MODE_PERIODIC,
	KTIMER_MODE_ONESHOT,
};

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

int rthal_arch_init(void);

void rthal_arch_cleanup(void);

extern unsigned long rthal_timerfreq_arg;

extern unsigned long rthal_clockfreq_arg;

int rthal_init(void);

void rthal_exit(void);

int rthal_apc_alloc(const char *name,
		    void (*handler)(void *cookie),
		    void *cookie);

void rthal_apc_free(int apc);

static inline void __rthal_apc_schedule(int apc)
{
	int cpu = ipipe_processor_id();
	if (!__test_and_set_bit(apc, &rthal_archdata.apc_pending[cpu]))
		ipipe_post_irq_root(rthal_archdata.apc_virq);
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

	flags = ipipe_test_and_stall_head() & 1;
	__rthal_apc_schedule(apc);
	ipipe_restore_head(flags);
}

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
