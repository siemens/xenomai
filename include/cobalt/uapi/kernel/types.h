/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_KERNEL_TYPES_H
#define _COBALT_UAPI_KERNEL_TYPES_H

typedef unsigned long long xnticks_t;

typedef long long xnsticks_t;

typedef unsigned long long xntime_t; /* ns */

typedef long long xnstime_t;

typedef unsigned long xnhandle_t;

#define XN_NO_HANDLE ((xnhandle_t)0)

#define XN_HANDLE_SPARE0	((xnhandle_t)0x10000000)
#define XN_HANDLE_SPARE1	((xnhandle_t)0x20000000)
#define XN_HANDLE_SPARE2	((xnhandle_t)0x40000000)
#define XN_HANDLE_SPARE3	((xnhandle_t)0x80000000)
#define XN_HANDLE_SPARE_MASK	((xnhandle_t)0xf0000000)

#define XNOBJECT_NAME_LEN 32

#endif /* !_COBALT_UAPI_KERNEL_TYPES_H */
