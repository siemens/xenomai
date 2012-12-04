/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _COBALT_FCNTL_H
#define _COBALT_FCNTL_H

#ifdef __KERNEL__

#include <linux/fcntl.h>

int open(const char *path, int oflag, ...);

#else /* !__KERNEL__ */

#include <time.h>
#include_next <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

COBALT_DECL(int, open(const char *path, int oflag, ...));

#ifdef __cplusplus
}
#endif

#endif /* !__KERNEL__ */

#endif /* !_COBALT_FCNTL_H */
