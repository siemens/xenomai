/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for PowerPC.
 *
 *   64-bit PowerPC adoption
 *     copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
 *
 *   Xenomai is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation, Inc., 675 Mass Ave,
 *   Cambridge MA 02139, USA; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   Xenomai is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */

/**
 * @addtogroup hal
 *
 * PowerPC-specific HAL services.
 *
 *@{*/

#undef DEBUG

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/console.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/xenomai/hal.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

static struct {

    unsigned long flags;
    int count;

} rthal_linux_irq[IPIPE_NR_XIRQS];

static int rthal_periodic_p;

/* the following two functions are very much alike to the I-pipe tune_timer
 * implementation, but tuned for critical_enter/exit usage
 * 
 * rthal_set_local_timer might come useful with processor hotplug events
 */
static void rthal_set_local_cpu_timer(void)
{
	long ticks;
	rthal_declare_cpuid;

	rthal_load_cpuid();

	disarm_decr[cpuid] = (__ipipe_decr_ticks != tb_ticks_per_jiffy);
#ifdef CONFIG_40x
	/* Enable and set auto-reload. */
	mtspr(SPRN_TCR, mfspr(SPRN_TCR) | TCR_ARE);
	mtspr(SPRN_PIT, __ipipe_decr_ticks);
#else	/* !CONFIG_40x */
	ticks = (long)(__ipipe_decr_next[cpuid] - __ipipe_read_timebase());
	set_dec(ticks > 0 ? ticks : 0);
#endif	/* CONFIG_40x */
	DBG("rthal_set_local_cpu_timer(%d): %ld\n", cpuid, ticks);
}

static int rthal_set_cpu_timers_unsafe(unsigned long ns)
{
	unsigned long ticks;
	unsigned long offset, previous_tb;
	int i;
	rthal_declare_cpuid;

	DBG("rthal_set_cpu_timers_unsafe: %lu\n", ns);
	
	if (ns == 0)
		ticks = tb_ticks_per_jiffy;
	else {
		ticks = ns * tb_ticks_per_jiffy / (1000000000 / HZ);

		if (ticks > tb_ticks_per_jiffy) {
			DBG("rthal_set_cpu_timers_unsafe: -EINVAL (%lu)\n", ticks);
			return -EINVAL;
		}
	}

	/* space timers on SMP to prevent lock contention in the handler */
	rthal_load_cpuid();
	offset = ticks/cpus_weight(cpu_online_map);
	DBG("rthal_set_cpu_timers_unsafe(%d): ticks=%lu offset=%lu\n", cpuid, ticks, offset);

	previous_tb = __ipipe_read_timebase() + ticks;
	__ipipe_decr_next[cpuid] = previous_tb;
	for_each_online_cpu(i) {
		if (i != cpuid) {
			__ipipe_decr_next[i] = previous_tb + offset;
			previous_tb = __ipipe_decr_next[i];
		}
	}
	__ipipe_decr_ticks = ticks;

	return 0;
}

static void rthal_critical_sync(void) {
#ifdef CONFIG_SMP
	rthal_declare_cpuid;
	
	rthal_load_cpuid();
	switch (rthal_sync_op) {
		case 1:
			/* timer_request */
			if (rthal_periodic_p) 
				rthal_set_local_cpu_timer();
			else
				disarm_decr[cpuid] = 1;
			break;
		case 2:
			/* timer_release */
			if (rthal_periodic_p)
				rthal_set_local_cpu_timer();
			else
				disarm_decr[cpuid] = 0;
				set_dec(tb_ticks_per_jiffy);
			
			break;
		case 3:
			/* cancel action */
			disarm_decr[cpuid] = 0;
			break;
	}
#endif /* CONFIG_SMP */
}

#ifdef CONFIG_SMP
static void rthal_smp_relay_tick(unsigned irq, void *cookie)
{
	rthal_irq_host_pend(RTHAL_TIMER_IRQ);
}
#endif /* CONFIG_SMP */

int rthal_timer_request (void (*handler)(void), 
		unsigned long nstick)
{
	unsigned long flags;
	int err = 0;
	rthal_declare_cpuid;

	flags = rthal_critical_enter(&rthal_critical_sync);

	rthal_sync_op = 1;

	if (nstick > 0) {
		/* Periodic setup. */
		err = rthal_set_cpu_timers_unsafe(nstick);
		rthal_periodic_p = 1;
	}
	else {
		/* Oneshot setup. */
		rthal_periodic_p = 0;
#ifdef CONFIG_40x
		mtspr(SPRN_TCR,mfspr(SPRN_TCR) & ~TCR_ARE); /* Auto-reload off. */
#endif /* CONFIG_40x */
		rthal_timer_program_shot(tb_ticks_per_jiffy);
	}
	rthal_load_cpuid();

	if (err) 
		goto out;

	rthal_irq_release(RTHAL_TIMER_IRQ);
	if ((err = rthal_irq_request(RTHAL_TIMER_IRQ,
			(rthal_irq_handler_t)handler,
			NULL,
			NULL)) < 0) {
		goto out;
	}

#ifdef CONFIG_SMP
	rthal_irq_release(RTHAL_TIMER_IPI);
	if ((err = rthal_irq_request(RTHAL_TIMER_IPI,
			(rthal_irq_handler_t)handler,
			NULL,
			NULL)) < 0) {
		rthal_irq_release(RTHAL_TIMER_IRQ);
		goto out;
	}
	rthal_irq_release(RTHAL_HOST_TIMER_IPI);
	if ((err = rthal_irq_request(RTHAL_HOST_TIMER_IPI,
			&rthal_smp_relay_tick,
			NULL,
			NULL)) < 0) {
		rthal_irq_release(RTHAL_TIMER_IRQ);
		goto out;
	}
#endif /* CONFIG_SMP */
	
	if (rthal_periodic_p)
		rthal_set_local_cpu_timer();
	else
		disarm_decr[cpuid] = 1;
	
out:
	if (err) {
		rthal_sync_op = 3;
		__ipipe_decr_ticks = tb_ticks_per_jiffy;
		disarm_decr[cpuid] = 0;
	}
	rthal_critical_exit(flags);
	
	return err;
}

void rthal_timer_release (void)
{
	unsigned long flags;
	rthal_declare_cpuid;

	flags = rthal_critical_enter(&rthal_critical_sync);
    
	rthal_sync_op = 2;

	rthal_load_cpuid();
    
	if (rthal_periodic_p)
		rthal_set_cpu_timers_unsafe(0);
	else {
#ifdef CONFIG_40x
		mtspr(SPRN_TCR,mfspr(SPRN_TCR)|TCR_ARE); /* Auto-reload on. */
		mtspr(SPRN_PIT,tb_ticks_per_jiffy);
#else /* !CONFIG_40x */
		set_dec(tb_ticks_per_jiffy);
#endif /* CONFIG_40x */
	}

#ifdef CONFIG_SMP
	rthal_irq_release(RTHAL_HOST_TIMER_IPI);
	rthal_irq_release(RTHAL_TIMER_IPI);
#endif /* CONFIG_SMP */
	rthal_irq_release(RTHAL_TIMER_IRQ);

	if (rthal_periodic_p)
		rthal_set_local_cpu_timer();
	else
		disarm_decr[cpuid] = 0;
    
	rthal_critical_exit(flags);
}

unsigned long rthal_timer_calibrate (void)

{
    return 1000000000 / RTHAL_CPU_FREQ;
}

int rthal_irq_host_request (unsigned irq,
			    irqreturn_t (*handler)(int irq,
						   void *dev_id,
						   struct pt_regs *regs), 
			    char *name,
			    void *dev_id)
{
    unsigned long flags;

    if (irq >= IPIPE_NR_XIRQS || !handler)
	return -EINVAL;

    spin_lock_irqsave(&rthal_irq_descp(irq)->lock,flags);

    if (rthal_linux_irq[irq].count++ == 0 && rthal_irq_descp(irq)->action)
	{
	rthal_linux_irq[irq].flags = rthal_irq_descp(irq)->action->flags;
	rthal_irq_descp(irq)->action->flags |= SA_SHIRQ;
	}

    spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock,flags);

    return request_irq(irq,handler,SA_SHIRQ,name,dev_id);
}

int rthal_irq_host_release (unsigned irq, void *dev_id)

{
    unsigned long flags;

    if (irq >= IPIPE_NR_XIRQS || rthal_linux_irq[irq].count == 0)
	return -EINVAL;

    free_irq(irq,dev_id);

    spin_lock_irqsave(&rthal_irq_descp(irq)->lock,flags);

    if (--rthal_linux_irq[irq].count == 0 && rthal_irq_descp(irq)->action)
	rthal_irq_descp(irq)->action->flags = rthal_linux_irq[irq].flags;

    spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock,flags);

    return 0;
}

int rthal_irq_enable (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (rthal_irq_descp(irq)->handler == NULL ||
	rthal_irq_descp(irq)->handler->enable == NULL)
	return -ENODEV;

    rthal_irq_descp(irq)->handler->enable(irq);

    return 0;
}

int rthal_irq_disable (unsigned irq)
{

    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (rthal_irq_descp(irq)->handler == NULL ||
	rthal_irq_descp(irq)->handler->disable == NULL)
	return -ENODEV;

    rthal_irq_descp(irq)->handler->disable(irq);

    return 0;
}

int rthal_irq_end (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (rthal_irq_descp(irq)->handler != NULL)
        {
	if (rthal_irq_descp(irq)->handler->end != NULL)
	    rthal_irq_descp(irq)->handler->end(irq);
	else if (rthal_irq_descp(irq)->handler->enable != NULL)
	    rthal_irq_descp(irq)->handler->enable(irq);
	else
	    return -ENODEV;
	}
    else
	return -ENODEV;

    return 0;
}

static inline int do_exception_event (unsigned event, unsigned domid, void *data)

{
    rthal_declare_cpuid;

    rthal_load_cpuid();

    if (domid == RTHAL_DOMAIN_ID)
	{
	rthal_realtime_faults[cpuid][event]++;

	if (rthal_trap_handler != NULL &&
	    test_bit(cpuid,&rthal_cpu_realtime) &&
	    rthal_trap_handler(event,domid,data) != 0)
	    return RTHAL_EVENT_STOP;
	}

    return RTHAL_EVENT_PROPAGATE;
}

RTHAL_DECLARE_EVENT(exception_event);

static inline void do_rthal_domain_entry (void)

{
    unsigned trapnr;

    /* Trap all faults. */
    for (trapnr = 0; trapnr < RTHAL_NR_FAULTS; trapnr++)
	rthal_catch_exception(trapnr,&exception_event);

    printk(KERN_INFO "Xenomai: hal/powerpc started.\n");
}

RTHAL_DECLARE_DOMAIN(rthal_domain_entry);

int rthal_arch_init (void)

{
#ifdef CONFIG_ALTIVEC
#ifdef CONFIG_PPC64
    if (!(cur_cpu_spec->cpu_features & CPU_FTR_ALTIVEC)) {
#else /* !CONFIG_PPC64 */
    if (!(cur_cpu_spec[0]->cpu_features & CPU_FTR_ALTIVEC)) {
#endif /* CONFIG_PPC64 */
        printk("Xenomai: ALTIVEC support enabled in kernel but no hardware found.\n"
               "         Disable CONFIG_ALTIVEC in the kernel configuration.\n");
	return -ENODEV;
    }
#endif /* CONFIG_ALTIVEC */

    if (rthal_cpufreq_arg == 0)
	/* The CPU frequency is expressed as the timebase frequency
	   for this port. */
	rthal_cpufreq_arg = (unsigned long)rthal_get_cpufreq();

    if (rthal_timerfreq_arg == 0)
	rthal_timerfreq_arg = rthal_cpufreq_arg;

    return 0;
}

void rthal_arch_cleanup (void)

{
    /* Nothing to cleanup so far. */
    printk(KERN_INFO "Xenomai: hal/powerpc stopped.\n");
}

/*@}*/

EXPORT_SYMBOL(rthal_arch_init);
EXPORT_SYMBOL(rthal_arch_cleanup);
EXPORT_SYMBOL(rthal_thread_switch);
EXPORT_SYMBOL(rthal_thread_trampoline);

#ifdef CONFIG_XENO_HW_FPU
EXPORT_SYMBOL(rthal_init_fpu);
EXPORT_SYMBOL(rthal_save_fpu);
EXPORT_SYMBOL(rthal_restore_fpu);
#endif /* CONFIG_XENO_HW_FPU */
