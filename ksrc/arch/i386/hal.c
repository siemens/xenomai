/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for x86.
 *
 *   Inspired from original RTAI/x86 HAL interface: \n
 *   Copyright &copy; 2000 Paolo Mantegazza, \n
 *   Copyright &copy; 2000 Steve Papacharalambous, \n
 *   Copyright &copy; 2000 Stuart Hughes, \n
 *
 *   RTAI/x86 rewrite over Adeos: \n
 *   Copyright &copy; 2002-2007 Philippe Gerum.
 *   NMI watchdog, SMI workaround: \n
 *   Copyright &copy; 2004 Gilles Chanteperdrix.
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
 * x86-specific HAL services.
 *
 *@{*/

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/console.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/desc.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif /* CONFIG_X86_IO_APIC */
#include <asm/apic.h>
#endif /* CONFIG_X86_LOCAL_APIC */
#include <asm/xenomai/hal.h>
#include <stdarg.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) && !defined(CONFIG_X86_TSC) && defined(CONFIG_VT)
#include <linux/vt_kern.h>

static void (*old_mksound) (unsigned int hz, unsigned int ticks);

static void dummy_mksound(unsigned int hz, unsigned int ticks)
{
}
#endif /* Linux < 2.6 && !CONFIG_X86_TSC && CONFIG_VT */

static struct {

	unsigned long flags;
	int count;

} rthal_linux_irq[IPIPE_NR_XIRQS];

#ifdef CONFIG_X86_LOCAL_APIC

static long long rthal_timers_sync_time;

static inline int rthal_set_apic_base(int lvtt_value)
{
	if (APIC_INTEGRATED(GET_APIC_VERSION(apic_read(APIC_LVR))))
		lvtt_value |= SET_APIC_TIMER_BASE(APIC_TIMER_BASE_DIV);

	return lvtt_value;
}

static inline void rthal_setup_periodic_apic(int count, int vector)
{
	apic_read(APIC_LVTT);
	apic_write_around(APIC_LVTT, rthal_set_apic_base(APIC_LVT_TIMER_PERIODIC | vector));
	apic_read(APIC_TMICT);
	apic_write_around(APIC_TMICT, count);
}

static inline void rthal_setup_oneshot_apic(int count, int vector)
{
	apic_read(APIC_LVTT);
	apic_write_around(APIC_LVTT, rthal_set_apic_base(vector));
}

static void rthal_critical_sync(void)
{
	long long sync_time;

	switch (rthal_sync_op) {
	case 1:
		sync_time = rthal_timers_sync_time;

		while (rthal_rdtsc() < sync_time) ;

		rthal_setup_oneshot_apic(RTHAL_APIC_ICOUNT,
					 RTHAL_APIC_TIMER_VECTOR);
		break;

	case 2:

		rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT,
					  LOCAL_TIMER_VECTOR);
		break;
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include <asm/smpboot.h>
static inline void send_IPI_all(int vector)
{
	unsigned long flags;

	rthal_local_irq_save_hw(flags);
	apic_wait_icr_idle();
	apic_write_around(APIC_ICR,
			  APIC_DM_FIXED | APIC_DEST_ALLINC | INT_DEST_ADDR_MODE
			  | vector);
	rthal_local_irq_restore_hw(flags);
}
#else
#include <mach_ipi.h>
#endif

DECLARE_LINUX_IRQ_HANDLER(rthal_broadcast_to_local_timers, irq, dev_id)
{
#ifdef CONFIG_SMP
	send_IPI_all(LOCAL_TIMER_VECTOR);
#else
	rthal_trigger_irq(ipipe_apic_vector_irq(LOCAL_TIMER_VECTOR));
#endif
	return IRQ_HANDLED;
}

unsigned long rthal_timer_calibrate(void)
{
	unsigned long flags;
	rthal_time_t t, dt;
	int i;

	flags = rthal_critical_enter(NULL);

	t = rthal_rdtsc();

	for (i = 0; i < 10000; i++) {
		apic_read(APIC_LVTT);
		apic_write_around(APIC_LVTT,
				  APIC_LVT_TIMER_PERIODIC | LOCAL_TIMER_VECTOR);
		apic_read(APIC_TMICT);
		apic_write_around(APIC_TMICT, RTHAL_APIC_ICOUNT);
	}

	dt = (rthal_rdtsc() - t) / 2;

	rthal_critical_exit(flags);

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
	/* reset the max trace, it contains the excessive calibration now */
	rthal_trace_max_reset();
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

	return rthal_imuldiv(dt, 100000, RTHAL_CPU_FREQ);
}

#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#include <linux/vt_kern.h>

extern void show_registers(struct pt_regs *regs);

extern spinlock_t nmi_print_lock;

void die_nmi(struct pt_regs *regs, const char *msg)
{
	spin_lock(&nmi_print_lock);
	/*
	 * We are in trouble anyway, lets at least try
	 * to get a message out.
	 */
	bust_spinlocks(1);
	printk(msg);
	show_registers(regs);
	printk("console shuts up ...\n");
	console_silent();
	spin_unlock(&nmi_print_lock);
	bust_spinlocks(0);
	do_exit(SIGSEGV);
}

#else /* Linux >= 2.6 */
#include <asm/nmi.h>
#endif /* Linux < 2.6 */

static void rthal_latency_above_max(struct pt_regs *regs)
{
	/* Try to report via latency tracer first, then fall back to panic. */
	if (rthal_trace_user_freeze(rthal_maxlat_us, 1) < 0) {
		char buf[128];

		snprintf(buf,
			 sizeof(buf),
			 "NMI watchdog detected timer latency above %u us\n",
			 rthal_maxlat_us);
		die_nmi(regs, buf);
	}
}

#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY */

int rthal_timer_request(
	void (*tick_handler)(void),
#ifdef CONFIG_GENERIC_CLOCKEVENTS
	void (*mode_emul)(enum clock_event_mode mode,
			  struct ipipe_tick_device *tdev),
	int (*tick_emul)(unsigned long delay,
			 struct ipipe_tick_device *tdev),
#endif
	int cpu)
{
	long long sync_time;
	unsigned long flags;
	int tickval;

	/* This code works both for UP+LAPIC and SMP configurations. */

#ifdef CONFIG_GENERIC_CLOCKEVENTS
	int err = ipipe_request_tickdev("lapic", mode_emul, tick_emul, cpu);

	switch (err) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* oneshot tick emulation callback won't be used, ask
		 * the caller to start an internal timer for emulating
		 * a periodic tick. */
		tickval = 1000000000UL / HZ;
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_ONESHOT:
		tickval = 0;
		break;

	default:
		return err;
	}
#else /* !CONFIG_GENERIC_CLOCKEVENTS */
	/*
	 * When the local APIC is enabled for kernels lacking generic
	 * support for clock events, we do not need to relay the host tick
	 * since 8254 interrupts are already flowing normally to Linux
	 * (i.e. the nucleus does not intercept them, but uses a dedicated
	 * APIC-based timer interrupt instead, i.e. RTHAL_APIC_TIMER_IPI).
	 */
	tickval = 0;
#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

	/*
	 * The rest of the initialization should only be performed
	 * once by a single CPU.
	 */
	if (cpu > 0)
		goto out;

	flags = rthal_critical_enter(rthal_critical_sync);

	rthal_sync_op = 1;

	rthal_timers_sync_time = rthal_rdtsc() +
	    rthal_imuldiv(LATCH, RTHAL_CPU_FREQ, CLOCK_TICK_RATE);

	sync_time = rthal_timers_sync_time;

	while (rthal_rdtsc() < sync_time)
		;

	rthal_setup_oneshot_apic(RTHAL_APIC_ICOUNT, RTHAL_APIC_TIMER_VECTOR);

	rthal_irq_request(RTHAL_APIC_TIMER_IPI,
			  (rthal_irq_handler_t) tick_handler, NULL, NULL);

	rthal_critical_exit(flags);

#ifndef CONFIG_GENERIC_CLOCKEVENTS
	rthal_irq_host_request(RTHAL_HOST_TICK_IRQ,
			       &rthal_broadcast_to_local_timers,
			       "rthal_broadcast_timer",
			       &rthal_broadcast_to_local_timers);
#endif

	rthal_nmi_init(&rthal_latency_above_max);
out:
	return tickval;
}

void rthal_timer_release(int cpu)
{
	unsigned long flags;

#ifdef CONFIG_GENERIC_CLOCKEVENTS
	ipipe_release_tickdev(cpu);
#else
	rthal_irq_host_release(RTHAL_HOST_TICK_IRQ,
			       &rthal_broadcast_to_local_timers);
#endif

	/*
	 * The rest of the cleanup work should only be performed once
	 * by a single CPU.
	 */
	if (cpu > 0)
		return;

#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY
	rthal_nmi_release();
#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY */

	flags = rthal_critical_enter(&rthal_critical_sync);

	rthal_sync_op = 2;
	rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT, LOCAL_TIMER_VECTOR);
	rthal_irq_release(RTHAL_APIC_TIMER_IPI);

	rthal_critical_exit(flags);
}

#else /* !CONFIG_X86_LOCAL_APIC */

unsigned long rthal_timer_calibrate(void)
{
	unsigned long flags;
	rthal_time_t t, dt;
	int i;

	flags = rthal_critical_enter(NULL);

	outb(0x34, PIT_MODE);

	t = rthal_rdtsc();

	for (i = 0; i < 10000; i++) {
		outb(LATCH & 0xff, PIT_CH0);
		outb(LATCH >> 8, PIT_CH0);
	}

	dt = rthal_rdtsc() - t;

	rthal_critical_exit(flags);

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
	/* reset the max trace, it contains the excessive calibration now */
	rthal_trace_max_reset();
#endif /* CONFIG_IPIPE_TRACE_IRQSOFF */

	return rthal_imuldiv(dt, 100000, RTHAL_CPU_FREQ);
}

int rthal_timer_request(
	void (*tick_handler)(void),
#ifdef CONFIG_GENERIC_CLOCKEVENTS
	void (*mode_emul)(enum clock_event_mode mode,
			  struct ipipe_tick_device *tdev),
	int (*tick_emul)(unsigned long delay,
			 struct ipipe_tick_device *tdev),
#endif
	int cpu)
{
	unsigned long flags;
	int tickval, err;

#ifdef CONFIG_GENERIC_CLOCKEVENTS
	err = ipipe_request_tickdev("pit", mode_emul, tick_emul, cpu);

	switch (err) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* oneshot tick emulation callback won't be used, ask
		 * the caller to start an internal timer for emulating
		 * a periodic tick. */
		tickval = 1000000000UL / HZ;
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_ONESHOT:
		tickval = 0;
		break;

	default:
		return err;
	}
#else /* !CONFIG_GENERIC_CLOCKEVENTS */
	/*
	 * Out caller has to to emulate the periodic host tick by its
	 * own means once we will have grabbed the PIT.
	 */
	tickval = 1000000000UL / HZ;
#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

	/* No APIC means that we can't be running in SMP mode, so this
	 * routine will be called only once, for CPU #0. */

	flags = rthal_critical_enter(NULL);

	/* Oneshot setup for 8254 channel #0. */
	outb(0x30, PIT_MODE);
	outb(LATCH & 0xff, PIT_CH0);
	outb(LATCH >> 8, PIT_CH0);

	rthal_irq_release(RTHAL_TIMER_IRQ);

	err = rthal_irq_request(RTHAL_TIMER_IRQ,
				(rthal_irq_handler_t)tick_handler, NULL, NULL);

	rthal_critical_exit(flags);

	return tickval;
}

void rthal_timer_release(int cpu)
{
	unsigned long flags;

	flags = rthal_critical_enter(NULL);

	outb(0x34, PIT_MODE);
	outb(LATCH & 0xff, PIT_CH0);
	outb(LATCH >> 8, PIT_CH0);
	rthal_irq_release(RTHAL_TIMER_IRQ);

	rthal_critical_exit(flags);
}

#endif /* CONFIG_X86_LOCAL_APIC */

#ifndef CONFIG_X86_TSC

static rthal_time_t rthal_tsc_8254;

static int rthal_last_8254_counter2;

/* TSC emulation using PIT channel #2. */

void rthal_setup_8254_tsc(void)
{
	unsigned long flags;
	int count;

	rthal_local_irq_save_hw(flags);

	outb_p(0x0, PIT_MODE);
	count = inb_p(PIT_CH0);
	count |= inb_p(PIT_CH0) << 8;
	outb_p(0xb4, PIT_MODE);
	outb_p(RTHAL_8254_COUNT2LATCH & 0xff, PIT_CH2);
	outb_p(RTHAL_8254_COUNT2LATCH >> 8, PIT_CH2);
	rthal_tsc_8254 = count + LATCH * jiffies;
	rthal_last_8254_counter2 = 0;
	/* Gate high, disable speaker */
	outb_p((inb_p(0x61) & ~0x2) | 1, 0x61);

	rthal_local_irq_restore_hw(flags);
}

rthal_time_t rthal_get_8254_tsc(void)
{
	unsigned long flags;
	int delta, count;
	rthal_time_t t;

	rthal_local_irq_save_hw(flags);

	outb(0xd8, PIT_MODE);
	count = inb(PIT_CH2);
	delta = rthal_last_8254_counter2 - (count |= (inb(PIT_CH2) << 8));
	rthal_last_8254_counter2 = count;
	rthal_tsc_8254 += (delta > 0 ? delta : delta + RTHAL_8254_COUNT2LATCH);
	t = rthal_tsc_8254;

	rthal_local_irq_restore_hw(flags);

	return t;
}

#endif /* !CONFIG_X86_TSC */

int rthal_irq_host_request(unsigned irq,
			   rthal_irq_host_handler_t handler,
			   char *name, void *dev_id)
{
	unsigned long flags;

	if (irq >= IPIPE_NR_XIRQS || !handler)
		return -EINVAL;

	spin_lock_irqsave(&rthal_irq_descp(irq)->lock, flags);

	if (rthal_linux_irq[irq].count++ == 0 && rthal_irq_descp(irq)->action) {
		rthal_linux_irq[irq].flags =
		    rthal_irq_descp(irq)->action->flags;
		rthal_irq_descp(irq)->action->flags |= IRQF_SHARED;
	}

	spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock, flags);

	return request_irq(irq, handler, IRQF_SHARED, name, dev_id);
}

int rthal_irq_host_release(unsigned irq, void *dev_id)
{
	unsigned long flags;

	if (irq >= NR_IRQS || rthal_linux_irq[irq].count == 0)
		return -EINVAL;

	free_irq(irq, dev_id);

	spin_lock_irqsave(&rthal_irq_descp(irq)->lock, flags);

	if (--rthal_linux_irq[irq].count == 0 && rthal_irq_descp(irq)->action)
		rthal_irq_descp(irq)->action->flags =
		    rthal_linux_irq[irq].flags;

	spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock, flags);

	return 0;
}

int rthal_irq_enable(unsigned irq)
{
	if (irq >= NR_IRQS)
		return -EINVAL;

	rthal_irq_desc_status(irq) &= ~IRQ_DISABLED;

	return rthal_irq_chip_enable(irq);
}

int rthal_irq_disable(unsigned irq)
{

	if (irq >= NR_IRQS)
		return -EINVAL;

	rthal_irq_desc_status(irq) |= IRQ_DISABLED;

	return rthal_irq_chip_disable(irq);
}

int rthal_irq_end(unsigned irq)
{
	if (irq >= NR_IRQS)
		return -EINVAL;

	return rthal_irq_chip_end(irq);
}

static inline int do_exception_event(unsigned event, unsigned domid, void *data)
{
	rthal_declare_cpuid;

	/* Notes:

	   1) GPF needs to be propagated downstream whichever domain caused
	   it. This is required so that we don't spuriously raise a fatal
	   error when some fixup code is available to solve the error
	   condition. For instance, Linux always attempts to reload the %gs
	   segment register when switching a process in (__switch_to()),
	   regardless of its value. It is then up to Linux's GPF handling
	   code to search for a possible fixup whenever some exception
	   occurs. In the particular case of the %gs register, such an
	   exception could be raised for an exiting process if a preemption
	   occurs inside a short time window, after the process's LDT has
	   been dropped, but before the kernel lock is taken.  The same goes
	   for Xenomai switching back a Linux thread in non-RT mode which
	   happens to have been preempted inside do_exit() after the MM
	   context has been dropped (thus the LDT too). In such a case, %gs
	   could be reloaded with what used to be the TLS descriptor of the
	   exiting thread, but unfortunately after the LDT itself has been
	   dropped. Since the default LDT is only 5 entries long, any attempt
	   to refer to an LDT-indexed descriptor above this value would cause
	   a GPF.  2) NMI is not pipelined. */

	rthal_load_cpuid();

	if (domid == RTHAL_DOMAIN_ID) {
		rthal_realtime_faults[cpuid][event]++;

		if (rthal_trap_handler != NULL &&
		    rthal_trap_handler(event, domid, data) != 0)
			return RTHAL_EVENT_STOP;
	}

	return RTHAL_EVENT_PROPAGATE;
}

RTHAL_DECLARE_EVENT(exception_event);

static inline void do_rthal_domain_entry(void)
{
	unsigned trapnr;

	/* Trap all faults. */
	for (trapnr = 0; trapnr < RTHAL_NR_FAULTS; trapnr++)
		rthal_catch_exception(trapnr, &exception_event);

	printk(KERN_INFO "Xenomai: hal/x86 started.\n");
}

RTHAL_DECLARE_DOMAIN(rthal_domain_entry);

int rthal_arch_init(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	if (!test_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability)) {
		printk("Xenomai: Local APIC absent or disabled!\n"
		       "         Disable APIC support or pass \"lapic=1\" as bootparam.\n");
		rthal_smi_restore();
		return -ENODEV;
	}
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) && !defined(CONFIG_X86_TSC) && defined(CONFIG_VT)
	/* Prevent the speaker code from bugging our TSC emulation, also
	   based on PIT channel 2. kd_mksound is exported by the Adeos
	   patch. */
	old_mksound = kd_mksound;
	kd_mksound = &dummy_mksound;
#endif /* !CONFIG_X86_LOCAL_APIC && Linux < 2.6 && !CONFIG_X86_TSC && CONFIG_VT */

	if (rthal_cpufreq_arg == 0)
#ifdef CONFIG_X86_TSC
		/* FIXME: 4Ghz barrier is close... */
		rthal_cpufreq_arg = rthal_get_cpufreq();
#else /* ! CONFIG_X86_TSC */
		rthal_cpufreq_arg = CLOCK_TICK_RATE;

	rthal_setup_8254_tsc();
#endif /* CONFIG_X86_TSC */

	if (rthal_timerfreq_arg == 0)
#ifdef CONFIG_X86_LOCAL_APIC
		rthal_timerfreq_arg = apic_read(APIC_TMICT) * HZ;
#else /* !CONFIG_X86_LOCAL_APIC */
		rthal_timerfreq_arg = CLOCK_TICK_RATE;
#endif /* CONFIG_X86_LOCAL_APIC */

	return 0;
}

void rthal_arch_cleanup(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) && !defined(CONFIG_X86_TSC) && defined(CONFIG_VT)
	/* Restore previous PC speaker code. */
	kd_mksound = old_mksound;
#endif /* Linux < 2.6 && !CONFIG_X86_TSC && CONFIG_VT */
	printk(KERN_INFO "Xenomai: hal/x86 stopped.\n");
}

/*@}*/

EXPORT_SYMBOL(rthal_arch_init);
EXPORT_SYMBOL(rthal_arch_cleanup);
#ifndef CONFIG_X86_TSC
EXPORT_SYMBOL(rthal_get_8254_tsc);
#endif /* !CONFIG_X86_TSC */
