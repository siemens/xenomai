/*
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
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

#ifndef _XENO_NUCLEUS_SYS_PPD_H
#define _XENO_NUCLEUS_SYS_PPD_H

#include <nucleus/ppd.h>
#include <nucleus/heap.h>

struct xnsys_ppd {
	struct xnshadow_ppd ppd;
	struct xnheap sem_heap;
	unsigned long mayday_addr;
	atomic_t refcnt;
	char *exe_path;
};

extern struct xnsys_ppd __xnsys_global_ppd;

static inline struct xnsys_ppd *xnsys_ppd_get(int global)
{
	struct xnshadow_ppd *ppd;

	if (global || (ppd = xnshadow_ppd_get(0)) == NULL)
		return &__xnsys_global_ppd;

	return container_of(ppd, struct xnsys_ppd, ppd);
}

#endif /* _XENO_NUCLEUS_SYS_PPD_H */
