/**
 *   Copyright &copy; 2012 Philippe Gerum.
 *
 *   Xenomai is free software; you can redistribute it and/or modify
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
#ifndef _COBALT_ASM_GENERIC_MACHINE_H
#define _COBALT_ASM_GENERIC_MACHINE_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/ipipe.h>
#include <linux/percpu.h>
#include <asm/byteorder.h>
#include <asm/xenomai/wrappers.h>

struct vm_area_struct;

struct xnarch_machdesc {
	const char *name;
	int (*init)(void);
	void (*cleanup)(void);
	void (*prefault)(struct vm_area_struct *vma);
	unsigned long (*calibrate)(void);
	const char *const *fault_labels;
};

extern struct xnarch_machdesc xnarch_machdesc;

struct xnarch_percpu_machdata {
	unsigned long apc_pending;
	unsigned long apc_shots[BITS_PER_LONG];
	unsigned int faults[IPIPE_NR_FAULTS];
};

DECLARE_PER_CPU(struct xnarch_percpu_machdata, xnarch_percpu_machdata);

struct xnarch_machdata {
	struct ipipe_domain domain;
	unsigned long timer_freq;
	unsigned long clock_freq;
	unsigned int apc_virq;
	unsigned long apc_map;
	unsigned int escalate_virq;
	struct {
		void (*handler)(void *cookie);
		void *cookie;
		const char *name;
	} apc_table[BITS_PER_LONG];
#ifdef CONFIG_SMP
	cpumask_t supported_cpus;
#endif
};

extern struct xnarch_machdata xnarch_machdata;

static inline unsigned long xnarch_timer_calibrate(void)
{
	return xnarch_machdesc.calibrate();
}

#ifdef CONFIG_SMP
#define xnarch_supported_cpus xnarch_machdata.supported_cpus

static inline int xnarch_cpu_supported(int cpu)
{
	return cpu_isset(cpu, xnarch_supported_cpus);
}
#else  /* !CONFIG_SMP */
#define xnarch_supported_cpus CPU_MASK_ALL

static inline int xnarch_cpu_supported(int cpu)
{
	return 1;
}
#endif /* !CONFIG_SMP */

#ifndef XNARCH_SHARED_HEAP_FLAGS
#define XNARCH_SHARED_HEAP_FLAGS  0
#endif

#endif /* !_COBALT_ASM_GENERIC_MACHINE_H */
