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
 *   SMI workaround: \n
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
 *
 *   i386-specific HAL services.
 */
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/desc.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/xenomai/hal.h>
#include <stdarg.h>
#include <asm/nmi.h>

#ifdef CONFIG_X86_LOCAL_APIC

unsigned long rthal_timer_calibrate(void)
{
	unsigned long flags, v;
	rthal_time_t t, dt;
	int i;

	flags = rthal_critical_enter(NULL);

	t = rthal_rdtsc();

	for (i = 0; i < 20; i++) {
		v = apic_read(APIC_TMICT);
		apic_write(APIC_TMICT, v);
	}

	dt = (rthal_rdtsc() - t) / 2;

	rthal_critical_exit(flags);

	/*
	 * Reset the max trace, since it contains the calibration time
	 * now.
	 */
	ipipe_trace_max_reset();

	return rthal_imuldiv(dt, 20, RTHAL_CLOCK_FREQ);
}

#else /* !CONFIG_X86_LOCAL_APIC */

extern enum rthal_ktimer_mode rthal_ktimer_saved_mode;

unsigned long rthal_timer_calibrate(void)
{
	unsigned long flags;
	rthal_time_t t, dt;
	int i, count;

	local_irq_save_hw(flags);

	/* Read the current latch value, whatever the current mode is. */

	outb_p(0x00, PIT_MODE);
	count = inb_p(PIT_CH0);
	count |= inb_p(PIT_CH0) << 8;

	if (count > LATCH) /* For broken VIA686a hardware. */
		count = LATCH - 1;
	/*
	 * We only want to measure the average time needed to program
	 * the next shot, so we basically don't care about the current
	 * PIT mode. We just rewrite the original latch value at each
	 * iteration.
	 */

	t = rthal_rdtsc();

	for (i = 0; i < 20; i++) {
		outb(count & 0xff, PIT_CH0);
		outb(count >> 8, PIT_CH0);
	}

	dt = rthal_rdtsc() - t;

	local_irq_restore_hw(flags);

	/*
	 * Reset the max trace, since it contains the calibration time
	 * now.
	 */
	ipipe_trace_max_reset();

	return rthal_imuldiv(dt, 20, RTHAL_CLOCK_FREQ);
}

static void rthal_timer_set_oneshot(void)
{
	unsigned long flags;
	int count;

	local_irq_save_hw(flags);
	/*
	 * We should be running in rate generator mode (M2) on entry,
	 * so read the current latch value, in order to roughly
	 * restart the timing where we left it, after the switch to
	 * software strobe mode.
	 */
	outb_p(0x00, PIT_MODE);
	count = inb_p(PIT_CH0);
	count |= inb_p(PIT_CH0) << 8;

	if (count > LATCH) /* For broken VIA686a hardware. */
		count = LATCH - 1;
	/*
	 * Force software triggered strobe mode (M4) on PIT channel
	 * #0.  We also program an initial shot at a sane value to
	 * restart the timing cycle.
	 */
	udelay(10);
	outb_p(0x38, PIT_MODE);
	outb(count & 0xff, PIT_CH0);
	outb(count >> 8, PIT_CH0);
	local_irq_restore_hw(flags);
}

static void rthal_timer_set_periodic(void)
{
	unsigned long flags;

	local_irq_save_hw(flags);
	outb_p(0x34, PIT_MODE);
	outb(LATCH & 0xff, PIT_CH0);
	outb(LATCH >> 8, PIT_CH0);
	local_irq_restore_hw(flags);
}

int rthal_timer_request(void (*tick_handler)(void),
			void (*mode_emul)(enum clock_event_mode mode,
					  struct clock_event_device *cdev),
			int (*tick_emul)(unsigned long delay,
					 struct clock_event_device *cdev),
			int cpu)
{
	int tickval, err;

	unsigned long tmfreq;

	int res = ipipe_request_tickdev("pit", mode_emul, tick_emul, cpu,
					&tmfreq);
	switch (res) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* oneshot tick emulation callback won't be used, ask
		 * the caller to start an internal timer for emulating
		 * a periodic tick. */
		tickval = 1000000000UL / HZ;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* oneshot tick emulation */
		tickval = 1;
		break;

	case CLOCK_EVT_MODE_UNUSED:
		/* we don't need to emulate the tick at all. */
		tickval = 0;
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
		return -ENOSYS;

	default:
		return res;
	}
	rthal_ktimer_saved_mode = res;

	if (rthal_timerfreq_arg == 0)
		rthal_archdata.timer_freq = tmfreq;
	/*
	 * No APIC means that we can't be running in SMP mode, so this
	 * routine will be called only once, for CPU #0.
	 */

	rthal_timer_set_oneshot();

	err = rthal_irq_request(RTHAL_TIMER_IRQ,
				(ipipe_irq_handler_t)tick_handler, NULL, NULL);

	return err ?: tickval;
}

void rthal_timer_release(int cpu)
{
	ipipe_release_tickdev(cpu);

	rthal_irq_release(RTHAL_TIMER_IRQ);

	if (rthal_ktimer_saved_mode == KTIMER_MODE_PERIODIC)
		rthal_timer_set_periodic();
	else if (rthal_ktimer_saved_mode == KTIMER_MODE_ONESHOT)
		/* We need to keep the timing cycle alive for the kernel. */
		ipipe_trigger_irq(RTHAL_TIMER_IRQ);
}

#endif /* !CONFIG_X86_LOCAL_APIC */

int rthal_arch_init(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	if (!boot_cpu_has(X86_FEATURE_APIC)) {
		printk("Xenomai: Local APIC absent or disabled!\n"
		       "         Disable APIC support or pass \"lapic=1\" as bootparam.\n");
		rthal_smi_restore();
		return -ENODEV;
	}
#endif /* CONFIG_X86_LOCAL_APIC */

	/* FIXME: 4Ghz barrier is close... */
	if (rthal_clockfreq_arg == 0)
		rthal_clockfreq_arg = rthal_get_clockfreq();

	return 0;
}

void rthal_arch_cleanup(void)
{
	printk(KERN_INFO "Xenomai: hal/i386 stopped.\n");
}

EXPORT_SYMBOL_GPL(rthal_arch_init);
EXPORT_SYMBOL_GPL(rthal_arch_cleanup);
