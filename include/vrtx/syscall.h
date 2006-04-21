/*
 * Copyright (C) 2006 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _XENO_VRTX_SYSCALL_H
#define _XENO_VRTX_SYSCALL_H

#ifndef __XENO_SIM__
#include <nucleus/bind.h>
#include <asm/xenomai/syscall.h>
#endif /* __XENO_SIM__ */

#define __vrtx_tecreate    0
#define __vrtx_tdelete     1
#define __vrtx_tpriority   2
#define __vrtx_tresume     3
#define __vrtx_tsuspend    4
#define __vrtx_tslice      5
#define __vrtx_tinquiry    6
#define __vrtx_lock        7
#define __vrtx_unlock      8

struct vrtx_arg_bulk {

    u_long a1;
    u_long a2;
    u_long a3;
};

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif

int vrtxsys_init(void);

void vrtxsys_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ */

#endif /* _XENO_VRTX_SYSCALL_H */
