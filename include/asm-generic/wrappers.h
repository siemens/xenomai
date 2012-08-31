/*
 * Copyright (C) 2005-2012 Philippe Gerum <rpm@xenomai.org>.
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
 * Generic wrappers.
 */

#ifndef _XENO_ASM_GENERIC_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ipipe_tickdev.h>
#include <asm/io.h>
#include <linux/pid.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
#error "Linux kernel 2.6.35 or above required"
#endif

#ifdef CONFIG_IPIPE_LEGACY
#error "CONFIG_IPIPE_LEGACY must be switched off"
#endif

static inline struct task_struct *wrap_find_task_by_pid(pid_t nr)
{
	return pid_task(find_pid_ns(nr, &init_pid_ns), PIDTYPE_PID);
}

#include <linux/semaphore.h>
#ifndef DEFINE_SEMAPHORE
/* Legacy DECLARE_MUTEX vanished in 2.6.37 */
#define DEFINE_SEMAPHORE(sem) DECLARE_MUTEX(sem)
#endif

#include <linux/mm.h>
#ifndef pgprot_noncached
#define pgprot_noncached(p) (p)
#endif /* !pgprot_noncached */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)

#define cpu_online_mask &(cpu_online_map)

#include <linux/sched.h>
#include <linux/mm.h>

static inline
unsigned long vm_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset)
{
	struct mm_struct *mm = current->mm;
	int ret;

	down_write(&mm->mmap_sem);
	ret = do_mmap(file, addr, len, prot, flag, offset);
	up_write(&mm->mmap_sem);

	return ret;
}

#endif /* LINUX_VERSION_CODE < 3.4.0 */

#endif /* _XENO_ASM_GENERIC_WRAPPERS_H */
