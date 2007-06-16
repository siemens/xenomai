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

#ifndef _XENO_ASM_GENERIC_BITS_POD_H
#define _XENO_ASM_GENERIC_BITS_POD_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#ifdef CONFIG_SMP

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
#if defined(CONFIG_SMP) && defined(MODULE)
	/* Make sure the shutdown sequence is kept on the same CPU
	   when running as a module. */
	set_cpus_allowed(current, cpumask_of_cpu(0));
#endif /* CONFIG_SMP && MODULE */
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

unsigned long long xnarch_get_sys_time(void)
{
    struct timeval tv;
    do_gettimeofday(&tv);
    return tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000;
}

EXPORT_SYMBOL(xnarch_get_sys_time);

#ifndef xnarch_tsc_to_ns
long long xnarch_tsc_to_ns(long long ts)
{
    return xnarch_llimd(ts,1000000000,RTHAL_CPU_FREQ);
}
#define xnarch_tsc_to_ns	xnarch_tsc_to_ns
#endif /* !xnarch_tsc_to_ns */

EXPORT_SYMBOL(xnarch_tsc_to_ns);

#ifndef xnarch_ns_to_tsc
long long xnarch_ns_to_tsc(long long ns)
{
    return xnarch_llimd(ns,RTHAL_CPU_FREQ,1000000000);
}
#define xnarch_ns_to_tsc	xnarch_ns_to_tsc
#endif /* !xnarch_ns_to_tsc */

EXPORT_SYMBOL(xnarch_ns_to_tsc);

unsigned long long xnarch_get_cpu_time(void)
{
    return xnarch_tsc_to_ns(xnarch_get_cpu_tsc());
}

EXPORT_SYMBOL(xnarch_get_cpu_time);

#endif /* !_XENO_ASM_GENERIC_BITS_POD_H */
