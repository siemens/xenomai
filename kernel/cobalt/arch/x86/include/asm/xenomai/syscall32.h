/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_X86_ASM_SYSCALL32_H
#define _COBALT_X86_ASM_SYSCALL32_H

#ifdef CONFIG_X86_X32

#include <linux/compat.h>

#define __COBALT_X32_BASE	128

#if __NR_COBALT_SYSCALLS >= __COBALT_X32_BASE
#error "__NR_COBALT_SYSCALLS >= __COBALT_X32_BASE"
#endif

/* 32bit call entry, assigning __handler to call #__name. */
#define __COBALT_CALL32_ENTRY(__name, __handler)		\
	, [sc_cobalt_ ## __name + __COBALT_X32_BASE] = __handler

/* 32bit thunk implementation. */
#define COBALT_SYSCALL32x(__name, __mode, __type, __args)	\
	__typeof__(__type) cobalt32x_ ## __name __args

/* 32bit thunk declaration. */
#define COBALT_SYSCALL32x_DECL(__name, __type, __args)	\
	__typeof__(__type) cobalt32x_ ## __name __args

#else /* !CONFIG_X86_X32 */

/* x32 support disabled. */

#define __COBALT_CALL32_ENTRY(__name, __handler)

#define COBALT_SYSCALL32x_DECL(__name, __type, __args)

#endif /* !CONFIG_X86_X32 */

#endif /* !_COBALT_X86_ASM_SYSCALL32_H */
