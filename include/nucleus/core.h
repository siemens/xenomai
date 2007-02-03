/*
 * Copyright (C) 2001,2002,2003,2004,2005 Philippe Gerum <rpm@xenomai.org>.
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
 * Core pod definitions. The core pod supports all APIs providing a
 * system call interface to user-space applications. Core APIs, namely
 * POSIX, native and RTDM, only use a sub-range of the available
 * priority levels of the core pod, in order to have them exhibit a
 * 1:1 mapping with Linux's SCHED_FIFO ascending priority
 * scale. Non-core APIs (e.g. VxWorks, VRTX) may also rely on the core
 * pod, provided they normalize the priority levels of their threads
 * when calling the nucleus, in order to match the priority scale
 * enforced by the former.
 */

#ifndef _XENO_NUCLEUS_CORE_H
#define _XENO_NUCLEUS_CORE_H

/* Visible priority range supported by the core pod. */
#define XNCORE_MIN_PRIO     0
#define XNCORE_MAX_PRIO     257

/* Total number of priority levels (including the hidden root one) */
#define XNCORE_NR_PRIO      (XNCORE_MAX_PRIO - XNCORE_MIN_PRIO + 2)

/* Priority sub-range used by core APIs. */
#define XNCORE_LOW_PRIO     0
#define XNCORE_HIGH_PRIO    99

/* Priority of IRQ servers in user-space. */
#define XNCORE_IRQ_PRIO     XNCORE_MAX_PRIO

/* Base priority of the root thread for the core pod. */
#define XNCORE_BASE_PRIO    -1

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int xncore_mount(void);

int xncore_umount(void);

int xncore_attach(void);

void xncore_detach(int xtype);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* __KERNEL__ */

#endif /* !_XENO_NUCLEUS_CORE_H */
