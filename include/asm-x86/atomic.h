/*
 * Copyright (C) 2007 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_X86_ATOMIC_H
#define _XENO_ASM_X86_ATOMIC_H

#include <asm/xenomai/features.h>

#ifdef __KERNEL__
#include <asm/atomic.h>

#define xnarch_atomic_set_mask(pflags,mask)		\
	atomic_set_mask((mask),(unsigned *)(pflags))
#define xnarch_atomic_clear_mask(pflags,mask)		\
	atomic_clear_mask((mask),(unsigned *)(pflags))

#else /* !__KERNEL */
#include <xeno_config.h>

#ifdef CONFIG_SMP
#define LOCK_PREFIX "lock ; "
#else
#define LOCK_PREFIX ""
#endif

#define cpu_relax() asm volatile("rep; nop" ::: "memory")

#ifdef __i386__

#define xnarch_memory_barrier()		__asm__ __volatile__("": : :"memory")
#define xnarch_read_memory_barrier() \
	__asm__ __volatile__ (LOCK_PREFIX "addl $0,0(%%esp)": : :"memory")
#define xnarch_write_memory_barrier() \
	__asm__ __volatile__ (LOCK_PREFIX "addl $0,0(%%esp)": : :"memory")

#else /* x86_64 */

#define xnarch_memory_barrier()		asm volatile("mfence":::"memory")
#define xnarch_read_memory_barrier()	asm volatile("lfence":::"memory")
#define xnarch_write_memory_barrier()	asm volatile("sfence":::"memory")

#endif /* x86_64 */

#endif /* !__KERNEL__ */

#include <asm-generic/xenomai/atomic.h>

#endif /* !_XENO_ASM_X86_ATOMIC_64_H */
