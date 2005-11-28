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
 *   Copyright &copy; 2002 Philippe Gerum.
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
#include <linux/interrupt.h>
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
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

extern struct desc_struct idt_table[];

#ifdef CONFIG_X86_LOCAL_APIC

static long long rthal_timers_sync_time;

struct rthal_apic_data {

    int mode;
    unsigned long count;
};

static struct rthal_apic_data rthal_timer_mode[RTHAL_NR_CPUS];

static inline void rthal_setup_periodic_apic (unsigned count,
                                              unsigned vector)
{
    apic_read(APIC_LVTT);
    apic_write_around(APIC_LVTT,APIC_LVT_TIMER_PERIODIC|vector);
    apic_read(APIC_TMICT);
    apic_write_around(APIC_TMICT,count);
}

static inline void rthal_setup_oneshot_apic (unsigned count,
                                             unsigned vector)
{
    apic_read(APIC_LVTT);
    apic_write_around(APIC_LVTT,vector);
    apic_read(APIC_TMICT);
    apic_write_around(APIC_TMICT,count);
}

static void rthal_critical_sync (void)

{
    struct rthal_apic_data *p;
    long long sync_time;
    rthal_declare_cpuid;

    switch (rthal_sync_op)
        {
        case 1:
            rthal_load_cpuid();

            p = &rthal_timer_mode[cpuid];

            sync_time = rthal_timers_sync_time;

            /* Stagger local timers on SMP systems, to prevent the
               tick handler from stupidly spinning while running on
               other CPU. */
            if(p->mode)
                sync_time += rthal_imuldiv(p->count, cpuid, num_online_cpus());

            while (rthal_rdtsc() < sync_time)
                ;
            
            if (p->mode)
                rthal_setup_periodic_apic(p->count,RTHAL_APIC_TIMER_VECTOR);
            else
                rthal_setup_oneshot_apic(p->count,RTHAL_APIC_TIMER_VECTOR);

            break;

        case 2:

            rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT,LOCAL_TIMER_VECTOR);
            break;
        }
}

irqreturn_t rthal_broadcast_to_local_timers (int irq,
                                             void *dev_id,
                                             struct pt_regs *regs)
{
    unsigned long flags;

    rthal_local_irq_save_hw(flags);
    apic_wait_icr_idle();
    apic_write_around(APIC_ICR,APIC_DM_FIXED|APIC_DEST_ALLINC|LOCAL_TIMER_VECTOR);
    rthal_local_irq_restore_hw(flags);

    return IRQ_HANDLED;
}

unsigned long rthal_timer_calibrate (void)

{
    unsigned long flags;
    rthal_time_t t, dt;
    int i;

    flags = rthal_critical_enter(NULL);

    t = rthal_rdtsc();

    for (i = 0; i < 10000; i++)
        { 
        apic_read(APIC_LVTT);
        apic_write_around(APIC_LVTT,APIC_LVT_TIMER_PERIODIC|LOCAL_TIMER_VECTOR);
        apic_read(APIC_TMICT);
        apic_write_around(APIC_TMICT,RTHAL_APIC_ICOUNT);
        }

    dt = rthal_rdtsc() - t;

    rthal_critical_exit(flags);

    return rthal_imuldiv(dt,100000,RTHAL_CPU_FREQ);
}

#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY
#include <asm/nmi.h>

unsigned long rthal_maxlat_tsc;
EXPORT_SYMBOL(rthal_maxlat_tsc);

static unsigned rthal_maxlat_us = CONFIG_XENO_HW_NMI_DEBUG_LATENCY_MAX;

static void rthal_latency_above_max(struct pt_regs *regs)
{
    char buf[128];
    snprintf(buf,
             sizeof(buf),
             "NMI watchdog detected timer latency above %u us\n",
             rthal_maxlat_us);
    die_nmi(regs, buf);
}
#endif

int rthal_timer_request (void (*handler)(void),
                         unsigned long nstick)
{
    struct rthal_apic_data *p;
    long long sync_time;
    unsigned long flags;
    rthal_declare_cpuid;
    int cpu;

    /* This code works both for UP+LAPIC and SMP configurations. */

    /* Try releasing the LAPIC-bound IRQ now so that any attempt to
       run a LAPIC-enabled configuration over a plain 8254-only/UP
       kernel will beget an error immediately. */

    if (rthal_irq_release(RTHAL_APIC_TIMER_IPI) < 0)
        return -EINVAL;

    flags = rthal_critical_enter(rthal_critical_sync);

    rthal_sync_op = 1;

    rthal_timers_sync_time = rthal_rdtsc() + rthal_imuldiv(LATCH,
                                                           RTHAL_CPU_FREQ,
                                                           CLOCK_TICK_RATE);

    /* We keep the setup data array just to be able to expose it to
       the visible interface if it happens to be really needed at some
       point in time. */
    
    for_each_online_cpu(cpu) {
        p = &rthal_timer_mode[cpu];
        p->mode = !!nstick;     /* 0=oneshot, 1=periodic */
        p->count = nstick;

        if (p->mode)
            p->count = rthal_imuldiv(p->count,RTHAL_TIMER_FREQ,1000000000);
        else
            p->count = RTHAL_APIC_ICOUNT;
    }

    rthal_load_cpuid();

    p = &rthal_timer_mode[cpuid];

    sync_time = rthal_timers_sync_time;

    if(p->mode)
        sync_time += rthal_imuldiv(p->count, cpuid, num_online_cpus());
    
    while (rthal_rdtsc() < sync_time)
        ;

    if (p->mode)
        rthal_setup_periodic_apic(p->count,RTHAL_APIC_TIMER_VECTOR);
    else
        rthal_setup_oneshot_apic(p->count,RTHAL_APIC_TIMER_VECTOR);

    rthal_irq_request(RTHAL_APIC_TIMER_IPI,
                      (rthal_irq_handler_t)handler,
                      NULL,
                      NULL);

    rthal_critical_exit(flags);

    rthal_irq_host_request(RTHAL_8254_IRQ,
                           &rthal_broadcast_to_local_timers,
                           "rthal_broadcast_timer",
                           &rthal_broadcast_to_local_timers);

#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY
    if (!p->mode) {
        rthal_maxlat_tsc = rthal_llimd(rthal_maxlat_us * 1000ULL,
                                       RTHAL_CPU_FREQ,
                                       1000000000);

        rthal_nmi_release();
    
        if (rthal_nmi_request(rthal_latency_above_max))
            printk("Xenomai: NMI watchdog not available.\n");
        else
            printk("Xenomai: NMI watchdog started.\n");
    }
#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY */

    return 0;
}

void rthal_timer_release (void)

{
    unsigned long flags;

#ifdef CONFIG_XENO_HW_NMI_DEBUG_LATENCY
    rthal_nmi_release();
#endif /* CONFIG_XENO_HW_NMI_DEBUG_LATENCY */

    rthal_irq_host_release(RTHAL_8254_IRQ,
                            &rthal_broadcast_to_local_timers);

    flags = rthal_critical_enter(&rthal_critical_sync);

    rthal_sync_op = 2;
    rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT,LOCAL_TIMER_VECTOR);
    rthal_irq_release(RTHAL_APIC_TIMER_IPI);

    rthal_critical_exit(flags);
}

#else /* !CONFIG_X86_LOCAL_APIC */

unsigned long rthal_timer_calibrate (void)

{
    unsigned long flags;
    rthal_time_t t, dt;
    int i;

    flags = rthal_critical_enter(NULL);

    outb(0x34,PIT_MODE);

    t = rthal_rdtsc();

    for (i = 0; i < 10000; i++)
        { 
        outb(LATCH & 0xff,PIT_CH0);
        outb(LATCH >> 8,PIT_CH0);
        }

    dt = rthal_rdtsc() - t;

    rthal_critical_exit(flags);

    return rthal_imuldiv(dt,100000,RTHAL_CPU_FREQ);
}

int rthal_timer_request (void (*handler)(void),
                         unsigned long nstick)
{
    unsigned long flags;
    int err;

    flags = rthal_critical_enter(NULL);

    if (nstick > 0)
        {
        /* Periodic setup for 8254 channel #0. */
        unsigned period;
        period = (unsigned)rthal_llimd(nstick,RTHAL_TIMER_FREQ,1000000000);
        if (period > LATCH) period = LATCH;
        outb(0x34,PIT_MODE);
        outb(period & 0xff,PIT_CH0);
        outb(period >> 8,PIT_CH0);
        }
    else
        {
        /* Oneshot setup for 8254 channel #0. */
        outb(0x30,PIT_MODE);
        outb(LATCH & 0xff,PIT_CH0);
        outb(LATCH >> 8,PIT_CH0);
        }

    rthal_irq_release(RTHAL_8254_IRQ);

    err = rthal_irq_request(RTHAL_8254_IRQ,
                            (rthal_irq_handler_t)handler,
                            NULL,
                            NULL);

    rthal_critical_exit(flags);

    return err;
}

void rthal_timer_release (void)

{
    unsigned long flags;

    flags = rthal_critical_enter(NULL);
    outb(0x34,PIT_MODE);
    outb(LATCH & 0xff,PIT_CH0);
    outb(LATCH >> 8,PIT_CH0);
    rthal_irq_release(RTHAL_8254_IRQ);

    rthal_critical_exit(flags);
}

#endif /* CONFIG_X86_LOCAL_APIC */

#ifndef CONFIG_X86_TSC

static rthal_time_t rthal_tsc_8254;

static int rthal_last_8254_counter2;

/* TSC emulation using PIT channel #2. */

void rthal_setup_8254_tsc (void)

{
    unsigned long flags;
    int count;

    rthal_local_irq_save_hw(flags);

    outb_p(0x0,PIT_MODE);
    count = inb_p(PIT_CH0);
    count |= inb_p(PIT_CH0) << 8;
    outb_p(0xb4,PIT_MODE);
    outb_p(RTHAL_8254_COUNT2LATCH & 0xff,PIT_CH2);
    outb_p(RTHAL_8254_COUNT2LATCH >> 8,PIT_CH2);
    rthal_tsc_8254 = count + LATCH * jiffies;
    rthal_last_8254_counter2 = 0; 
    /* Gate high, disable speaker */
    outb_p((inb_p(0x61)&~0x2)|1,0x61);

    rthal_local_irq_restore_hw(flags);
}

rthal_time_t rthal_get_8254_tsc (void)

{
    unsigned long flags;
    int delta, count;
    rthal_time_t t;

    rthal_local_irq_save_hw(flags);

    outb(0xd8,PIT_MODE);
    count = inb(PIT_CH2);
    delta = rthal_last_8254_counter2 - (count |= (inb(PIT_CH2) << 8));
    rthal_last_8254_counter2 = count;
    rthal_tsc_8254 += (delta > 0 ? delta : delta + RTHAL_8254_COUNT2LATCH);
    t = rthal_tsc_8254;

    rthal_local_irq_restore_hw(flags);

    return t;
}

#endif /* !CONFIG_X86_TSC */

static inline int do_exception_event (unsigned event, unsigned domid, void *data)

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

    printk(KERN_INFO "Xenomai: hal/x86 started.\n");
}

RTHAL_DECLARE_DOMAIN(rthal_domain_entry);

int rthal_arch_init (void)

{
#ifdef CONFIG_X86_LOCAL_APIC
    if (!test_bit(X86_FEATURE_APIC,boot_cpu_data.x86_capability))
    {
        printk("Xenomai: Local APIC absent or disabled!\n"
               "         Disable APIC support or pass \"lapic\" as bootparam.\n");
        rthal_smi_restore();
        return -ENODEV;
    }
#endif /* CONFIG_X86_LOCAL_APIC */

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

void rthal_arch_cleanup (void)

{
    printk(KERN_INFO "Xenomai: hal/x86 stopped.\n");
}

/*@}*/

EXPORT_SYMBOL(rthal_arch_init);
EXPORT_SYMBOL(rthal_arch_cleanup);
#ifndef CONFIG_X86_TSC
EXPORT_SYMBOL(rthal_get_8254_tsc);
#endif /* !CONFIG_X86_TSC */
