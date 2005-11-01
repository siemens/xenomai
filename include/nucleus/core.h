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
 */

#ifndef _XENO_NUCLEUS_CORE_H
#define _XENO_NUCLEUS_CORE_H

/* Thread priority levels. */
#define XNCORE_LOW_PRIO     1
#define XNCORE_HIGH_PRIO    99
/* Extra level for IRQ servers in user-space. */
#define XNCORE_IRQ_PRIO     (XNCORE_HIGH_PRIO + 1)

#define XNCORE_MIN_PRIO     XNCORE_LOW_PRIO
#define XNCORE_MAX_PRIO     XNCORE_IRQ_PRIO

#ifdef __KERNEL__

#include <linux/config.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int xncore_mount(void);

int xncore_umount(void);

int xncore_attach(void);

int xncore_detach(void);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* __KERNEL__ */

#endif /* !_XENO_NUCLEUS_CORE_H */
