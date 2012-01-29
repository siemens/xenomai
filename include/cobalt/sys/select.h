/*
 * Copyright (C) 2010 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
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

#ifndef _COBALT_SELECT_H
#define _COBALT_SELECT_H

#ifndef __KERNEL__

#include_next <sys/select.h>
#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif

COBALT_DECL(int, select(int __nfds, fd_set *__restrict __readfds,
			fd_set *__restrict __writefds,
			fd_set *__restrict __exceptfds,
			struct timeval *__restrict __timeout));
#ifdef __cplusplus
}
#endif

#endif /* !__KERNEL__ */

#endif /* !_COBALT_SELECT_H */
