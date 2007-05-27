/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _uITRON_defs_h
#define _uITRON_defs_h

#include "nucleus/xenomai.h"
#include "uitron/uitron.h"

#define ui_h2obj(h,m,t) \
((h) && ((t *)(h))->magic == (m) ? ((t *)(h)) : NULL)

#define ui_mark_deleted(t) ((t)->magic = 0)

#define ui_isobj(h) ((h) && \
((*((unsigned *)(h)) & 0xffff0000) == 0x85850000)

/* The following macros return normalized or native priority values
   for the underlying pod. The core pod providing user-space support
   uses an ascending [0..257] priority scale (include/nucleus/core.h),
   whilst the uITRON personality exhibits a decreasing scale
   [8..1]. We normalize to the range [92..99]. */

#define ui_normalized_prio(prio)	(XNCORE_HIGH_PRIO - (prio) + 1)
#define ui_denormalized_prio(prio)	(uITRON_MAX_PRI - (XNCORE_HIGH_PRIO - prio))

#endif /* !_uITRON_defs_h */
