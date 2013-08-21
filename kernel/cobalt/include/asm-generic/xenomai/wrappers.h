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
 */
#ifndef _COBALT_ASM_GENERIC_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ipipe.h>
#include <linux/ipipe_tickdev.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <linux/pid.h>

#ifdef CONFIG_IPIPE_LEGACY
#error "CONFIG_IPIPE_LEGACY must be switched off"
#endif

#include <linux/mm.h>
#ifndef pgprot_noncached
#define pgprot_noncached(p) (p)
#endif /* !pgprot_noncached */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)

#include <linux/smp.h>

#ifndef cpu_online_map
#define cpu_online_mask (&cpu_online_map)
#endif

static inline
unsigned long vm_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset)
{
	struct mm_struct *mm = current->mm;
	unsigned long ret;

	down_write(&mm->mmap_sem);
	ret = do_mmap(file, addr, len, prot, flag, offset);
	up_write(&mm->mmap_sem);

	return ret;
}

#endif /* LINUX_VERSION_CODE < 3.4.0 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
#define KGIDT_INIT(pid) (pid)
#endif /* LINUX < 3.8.0 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)
#include <linux/sched/rt.h>
#endif /* LINUX >= 3.9.0 */
 
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#include <linux/proc_fs.h>

#define PDE_DATA(inode)	PROC_I(inode)->pde->data

static inline void proc_remove(struct proc_dir_entry *pde)
{
	remove_proc_entry(pde->name, pde->parent);
}
#endif /* < 3.10 */

#endif /* _COBALT_ASM_GENERIC_WRAPPERS_H */
