/*
 * Copyright (C) 2001-2006 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_NUCLEUS_COMPILER_H
#define _XENO_NUCLEUS_COMPILER_H

#ifdef __KERNEL__
#include <linux/compiler.h>
#define __deprecated__ __deprecated
#define __deprecated_in_userspace__
#define __deprecated_in_kernel__  __deprecated__
#else /* !__KERNEL__ */
#if __GNUC__ >= 3 && __GNUC_MINOR__ > 0
#define __deprecated__	__attribute__((deprecated))
#else
#define __deprecated__	/* Unimplemented */
#endif
#ifndef __XENO_SIM__
#define __deprecated_in_userspace__ __deprecated__
#define __deprecated_in_kernel__
#else /* __XENO_SIM__ */
#define __deprecated_in_userspace__
#define __deprecated_in_kernel__    __deprecated__
#endif /* !__XENO_SIM__ */
#endif /* __KERNEL__ */

#endif /* !_XENO_NUCLEUS_COMPILER_H */
