/**
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2001-2013 The Xenomai project <http://www.xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
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
 *
 * @addtogroup nucleus
 * @{
 */
#ifndef _COBALT_KERNEL_SYS_H
#define _COBALT_KERNEL_SYS_H

#include <asm/xenomai/machine.h>

struct module;
struct xnshadow_ppd;
struct xnsyscall;
struct xnthread;

struct xnpersonality {
	const char *name;
	unsigned int magic;
	int nrcalls;
	struct xnsyscall *syscalls;
	atomic_t refcnt;
	struct {
		struct xnshadow_ppd *(*attach_process)(void);
		void (*detach_process)(struct xnshadow_ppd *ppd);
		struct xnpersonality *(*map_thread)(struct xnthread *thread);
		struct xnpersonality *(*exit_thread)(struct xnthread *thread);
		struct xnpersonality *(*finalize_thread)(struct xnthread *thread);
	} ops;
	struct module *module;
};

#ifdef CONFIG_SMP

#define xnsys_cpus xnarch_machdata.supported_cpus

static inline int xnsys_supported_cpu(int cpu)
{
	return cpu_isset(cpu, xnsys_cpus);
}

#else  /* !CONFIG_SMP */

#define xnsys_cpus CPU_MASK_ALL

static inline int xnsys_supported_cpu(int cpu)
{
	return 1;
}

#endif /* !CONFIG_SMP */

#define for_each_xenomai_cpu(cpu)		\
	for_each_online_cpu(cpu)		\
		if (xnsys_supported_cpu(cpu))	\

extern cpumask_t nkaffinity;

extern struct xnpersonality xenomai_personality;

int xnsys_init(void);

void xnsys_shutdown(void);

/* @} */

#endif /* !_COBALT_KERNEL_SYS_H */
