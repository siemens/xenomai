/*
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _COBALT_KERNEL_TYPES_H
#define _COBALT_KERNEL_TYPES_H

#include <cobalt/uapi/sys/types.h>

#define setbits(flags,mask)  xnarch_atomic_set_mask(&(flags),mask)
#define clrbits(flags,mask)  xnarch_atomic_clear_mask(&(flags),mask)
#define __clrbits(flags,mask)  do { (flags) &= ~(mask); } while(0)

#define XENO_INFO KERN_INFO    "[Xenomai] "
#define XENO_WARN KERN_WARNING "[Xenomai] "
#define XENO_ERR  KERN_ERR     "[Xenomai] "

#endif /* !_COBALT_KERNEL_TYPES_H */
