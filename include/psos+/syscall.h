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

#ifndef _XENO_PSOS_SYSCALL_H
#define _XENO_PSOS_SYSCALL_H

#ifndef __XENO_SIM__
#include <asm/xenomai/syscall.h>
#endif /* __XENO_SIM__ */

#define __psos_t_create    0
#define __psos_t_start     1
#define __psos_t_delete    2
#define __psos_t_suspend   3
#define __psos_t_resume    4
#define __psos_t_ident     5
#define __psos_t_mode      6

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif

int psos_syscall_init(void);

void psos_syscall_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ */

#endif /* _XENO_PSOS_SYSCALL_H */
