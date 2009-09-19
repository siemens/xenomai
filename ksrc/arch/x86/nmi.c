/**
 *   @ingroup hal
 *   @file
 *
 *   NMI watchdog for x86, from linux/arch/i386/kernel/nmi.c
 *
 *   Original authors:
 *   Ingo Molnar, Mikael Pettersson, Pavel Machek.
 *
 *   Adaptation to Xenomai by Gilles Chanteperdrix
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

#include <linux/module.h>
#include <linux/version.h>
#include <linux/nmi.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
#include <asm/perf_counter.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
#include <asm/intel_arch_perfmon.h>
#endif /* Linux < 2.6.19 */
#include <asm/nmi.h>
#endif /* Linux < 2.6 */
#include <asm/msr.h>
#include <asm/xenomai/hal.h>

#define NMI_WD_ARMED		0x0001
#define NMI_WD_31BITS		0x1000
#define NMI_WD_P4		0x2000
#define NMI_WD_P6_OR_LATER	0x4000

#define P4_ESCR_EVENT_SELECT(N) ((N)<<25)
#define P4_ESCR_OS              (1<<3)
#define P4_ESCR_USR             (1<<2)
#define P4_CCCR_OVF_PMI0        (1<<26)
#define P4_CCCR_OVF_PMI1        (1<<27)
#define P4_CCCR_THRESHOLD(N)    ((N)<<20)
#define P4_CCCR_COMPLEMENT      (1<<19)
#define P4_CCCR_COMPARE         (1<<18)
#define P4_CCCR_REQUIRED        (3<<16)
#define P4_CCCR_ESCR_SELECT(N)  ((N)<<13)
#define P4_CCCR_ENABLE          (1<<12)
/* Set up IQ_COUNTER0 to behave like a clock, by having IQ_CCCR0 filter
   CRU_ESCR0 (with any non-null event selector) through a complemented
   max threshold. [IA32-Vol3, Section 14.9.9] */
#define MSR_P4_IQ_COUNTER0      0x30C
#define P4_NMI_CRU_ESCR0        (P4_ESCR_EVENT_SELECT(0x3F)|P4_ESCR_OS|P4_ESCR_USR)
#define P4_NMI_IQ_CCCR0                                                 \
	(P4_CCCR_OVF_PMI0|P4_CCCR_THRESHOLD(15)|P4_CCCR_COMPLEMENT|	\
	 P4_CCCR_COMPARE|P4_CCCR_REQUIRED|P4_CCCR_ESCR_SELECT(4)|P4_CCCR_ENABLE)

typedef union {
	struct {
		/* Xenomai watchdog data. */
		unsigned long long next_linux_check;
		unsigned long perfctr_msr;
		u64 perfctr_checkmask;
		unsigned int p4_cccr_val;
		unsigned int flags;
	};
	char __pad[SMP_CACHE_BYTES];
} rthal_nmi_wd_t ____cacheline_aligned;

static rthal_nmi_wd_t rthal_nmi_wds[NR_CPUS];
static void (*rthal_nmi_emergency) (struct pt_regs *);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
#define MSR_ARCH_PERFMON_PERFCTR0	0xc1
#define MSR_ARCH_PERFMON_PERFCTR1	0xc2
union cpuid10_eax {
	struct {
		unsigned int version_id:8;
		unsigned int num_counters:8;
		unsigned int bit_width:8;
		unsigned int mask_length:8;
	} split;
	unsigned int full;
};
static void (*rthal_linux_nmi_tick) (struct pt_regs *);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define MSR_P4_IQ_CCCR0		0x36C
#define rthal_nmi_active (nmi_watchdog != NMI_NONE)
static inline void wrmsrl(unsigned long msr, unsigned long long val)
{
	unsigned long lo, hi;
	lo = (unsigned long)val;
	hi = val >> 32;
	wrmsr(msr, lo, hi);
}
#else /* Linux 2.6.0..18 */
extern int nmi_active;
#define rthal_nmi_active	nmi_active
#endif /* Linux 2.6.0..18 */

#else /* Linux >= 2.6.19 */
static int (*rthal_linux_nmi_tick) (struct pt_regs *, unsigned);
#define rthal_nmi_active	atomic_read(&nmi_active)
#endif /* Linux >= 2.6.19 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
#define CALL_LINUX_NMI		({ rthal_linux_nmi_tick(regs); 1; })
#define NMI_RETURN(code)	return
static void rthal_nmi_watchdog_tick(struct pt_regs *regs)
#else /* Linux >= 2.6.19 */
#define CALL_LINUX_NMI		rthal_linux_nmi_tick(regs, reason)
#define NMI_RETURN(code)	return (code)
static int rthal_nmi_watchdog_tick(struct pt_regs *regs, unsigned reason)
#endif /* Linux >= 2.6.19 */
{
	int cpu = rthal_processor_id();
	rthal_nmi_wd_t *wd = &rthal_nmi_wds[cpu];
	unsigned long long now;
	u64 perfctr;

	rdmsrl(wd->perfctr_msr, perfctr);

	if (perfctr & wd->perfctr_checkmask)
		/* No watchdog tick, let Linux handle it. */
		NMI_RETURN(CALL_LINUX_NMI);

	if (wd->flags & NMI_WD_ARMED)
		rthal_nmi_emergency(regs);

	now = rthal_rdtsc();

	if ((long long)(now - wd->next_linux_check) >= 0) {

		CALL_LINUX_NMI;

		do {
			wd->next_linux_check += RTHAL_CPU_FREQ;
		} while ((long long)(now - wd->next_linux_check) >= 0);
	}

	if (wd->flags & NMI_WD_P4) {
		/*
		 * P4 quirks:
		 * - An overflown perfctr will assert its interrupt
		 *   until the OVF flag in its CCCR is cleared.
		 * - LVTPC is masked on interrupt and must be
		 *   unmasked by the LVTPC handler.
		 */
		wrmsr(MSR_P4_IQ_CCCR0, wd->p4_cccr_val, 0);
		apic_write(APIC_LVTPC, APIC_DM_NMI);
	} else if (wd->flags & NMI_WD_P6_OR_LATER) {
		/* P6 based Pentium M need to re-unmask
		 * the apic vector but it doesn't hurt
		 * other P6 variant.
		 * ArchPerfom/Core Duo also needs this */
		apic_write(APIC_LVTPC, APIC_DM_NMI);
	}

	if (wd->flags & NMI_WD_31BITS)
		wrmsr(wd->perfctr_msr, (u32)(now - wd->next_linux_check), 0);
	else
		wrmsrl(wd->perfctr_msr, now - wd->next_linux_check);

	NMI_RETURN(1);
}

int rthal_nmi_request(void (*emergency) (struct pt_regs *))
{
	unsigned long long next_linux_check;
	unsigned long perfctr_msr;
	u64 perfctr_checkmask;
	unsigned int wd_flags = 0;
	unsigned int p4_cccr_val = 0;
	int i;

	if (!rthal_nmi_active || !nmi_watchdog_tick)
		return -ENODEV;

	if (rthal_linux_nmi_tick)
		return -EBUSY;

	switch (boot_cpu_data.x86_vendor) {
        case X86_VENDOR_AMD:
		perfctr_msr = MSR_K7_PERFCTR0;
		perfctr_checkmask = 1ULL << 47;
		break;
        case X86_VENDOR_INTEL:
		if (cpu_has(&boot_cpu_data, X86_FEATURE_ARCH_PERFMON)) {
			union cpuid10_eax eax;

			if (boot_cpu_data.x86 == 6 &&
			    boot_cpu_data.x86_model == 14)
				perfctr_msr = MSR_ARCH_PERFMON_PERFCTR0;
			else
				perfctr_msr = MSR_ARCH_PERFMON_PERFCTR1;
			cpuid(10, &eax.full, &i, &i, &i);
			perfctr_checkmask = 1ULL << (eax.split.bit_width - 1);
			wd_flags = NMI_WD_P6_OR_LATER | NMI_WD_31BITS;
		} else
			switch (boot_cpu_data.x86) {
	                case 6:
				perfctr_msr = MSR_P6_PERFCTR0;
				perfctr_checkmask = 1ULL << 39;
				wd_flags = NMI_WD_P6_OR_LATER | NMI_WD_31BITS;
				break;
	                case 15:
				perfctr_msr = MSR_P4_IQ_COUNTER0;
				perfctr_checkmask = 1ULL << 39;
				p4_cccr_val = P4_NMI_IQ_CCCR0;
#ifdef CONFIG_SMP
				if (smp_num_siblings == 2)
					p4_cccr_val |= P4_CCCR_OVF_PMI1;
#endif
				break;
	                default:
				return -ENODEV;
			}
		break;
        default:
		return -ENODEV;
	}

	rthal_nmi_emergency = emergency;

	next_linux_check = rthal_rdtsc() + RTHAL_CPU_FREQ;
	for (i = 0; i < NR_CPUS; i++) {
		rthal_nmi_wd_t *wd = &rthal_nmi_wds[i];

		wd->flags = wd_flags;
		wd->perfctr_msr = perfctr_msr;
		wd->perfctr_checkmask = perfctr_checkmask;
		wd->p4_cccr_val = p4_cccr_val;
		wd->next_linux_check = next_linux_check;
	}

	rthal_linux_nmi_tick = nmi_watchdog_tick;
	wmb();
	nmi_watchdog_tick = &rthal_nmi_watchdog_tick;

	return 0;
}

void rthal_nmi_release(void)
{
	rthal_nmi_wd_t *wd = &rthal_nmi_wds[rthal_processor_id()];
	int i;

	if (!rthal_linux_nmi_tick)
		return;

	if (wd->flags & NMI_WD_31BITS)
		wrmsr(wd->perfctr_msr, (u32)(0 - RTHAL_CPU_FREQ), 0);
	else
		wrmsrl(wd->perfctr_msr, 0 - RTHAL_CPU_FREQ);
	for (i = 0; i < NR_CPUS; i++)
		rthal_nmi_wds[i].perfctr_msr = 0;
	touch_nmi_watchdog();
	wmb();
	nmi_watchdog_tick = rthal_linux_nmi_tick;
	rthal_linux_nmi_tick = NULL;
}

void rthal_nmi_arm(unsigned long delay)
{
	rthal_nmi_wd_t *wd = &rthal_nmi_wds[rthal_processor_id()];

	if (!wd->perfctr_msr)
		return;

	/* If linux watchdog could tick now, make it tick now. */
	if ((long long) (rthal_rdtsc() - wd->next_linux_check) >= 0) {
		unsigned long flags;

		/* Protect from an interrupt handler calling rthal_nmi_arm. */
		rthal_local_irq_save(flags);
		wd->flags &= ~NMI_WD_ARMED;
		/*
		 * Our watchdog must be declared unarmed before we triger the
		 * Linux watchdog NMI, entering rthal_nmi_watchdog_tick.
		 */
		wmb();
		if (wd->flags & NMI_WD_31BITS)
			wrmsr(wd->perfctr_msr, (u32)-1, 0);
		else
			wrmsrl(wd->perfctr_msr, -1);
		asm("nop");
		rthal_local_irq_restore(flags);
	}

	if (wd->flags & NMI_WD_31BITS)
		wrmsr(wd->perfctr_msr, (u32)(0 - delay), 0);
	else
		wrmsrl(wd->perfctr_msr, 0 - delay);
	/*
	 * New perfctr must have been written before we can declare the
	 * watchdog armed (avoid race with previously programmed value).
	 */
	wmb();
	wd->flags |= NMI_WD_ARMED;
}

void rthal_nmi_disarm(void)
{
	rthal_nmi_wds[rthal_processor_id()].flags &= ~NMI_WD_ARMED;
}

EXPORT_SYMBOL(rthal_nmi_request);
EXPORT_SYMBOL(rthal_nmi_release);
EXPORT_SYMBOL(rthal_nmi_arm);
EXPORT_SYMBOL(rthal_nmi_disarm);
