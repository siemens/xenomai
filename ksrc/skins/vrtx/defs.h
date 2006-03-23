/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003,2006 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_VRTX_DEFS_H
#define _XENO_VRTX_DEFS_H

#include <nucleus/xenomai.h>
#include <nucleus/registry.h>
#include <vrtx/vrtx.h>

/* Those should be ^2s and even multiples of BITS_PER_LONG when
   reservation is applicable. */
#define VRTX_MAX_EVENTS  256
#define VRTX_MAX_HEAPS   256
#define VRTX_MAX_MUTEXES 256
#define VRTX_MAX_PTS     256
#define VRTX_MAX_SEMS    256
#define VRTX_MAX_QUEUES  256
#define VRTX_MAX_NTASKS  512	/* Named tasks -- anonymous ones aside. */

#define VRTX_MAX_IDS     512 /* # of available ids per object type. */

#if BITS_PER_LONG * BITS_PER_LONG < VRTX_MAX_IDS
#error "Internal bitmap cannot hold so many priority levels"
#endif

#define vrtx_h2obj_active(h,m,t) \
((h) && ((t *)(h))->magic == (m) ? ((t *)(h)) : NULL)

#define vrtx_mark_deleted(t) ((t)->magic = ~(t)->magic)

#define __IDMAP_LONGS ((VRTX_MAX_IDS+BITS_PER_LONG-1)/BITS_PER_LONG)

typedef struct vrtxidmap {

    int maxids;
    int usedids;
    unsigned long himask;
    unsigned long himap;
    unsigned long lomap[__IDMAP_LONGS];
    void *objarray[1];

} vrtxidmap_t;

#undef __IDMAP_LONGS

/* The following macros return normalized or native priority values
   for the underlying pod. The core pod providing user-space support
   uses an ascending [0-256] priority scale (include/nucleus/core.h),
   whilst the VRTX personality exhibits a decreasing scale
   [255-0]. Normalization is not needed when the underlying pod
   supporting the VRTX skin is standalone, i.e. pure kernel, UVM or
   simulation modes. */

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
#define vrtx_normalized_prio(prio)	(XNCORE_MAX_PRIO - (prio) - 1)
#define vrtx_denormalized_prio(prio)	(255 - (prio))
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
#define vrtx_normalized_prio(prio)	prio
#define vrtx_denormalized_prio(prio)	prio
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

#ifdef __cplusplus
extern "C" {
#endif

vrtxidmap_t *vrtx_alloc_idmap(int maxids,
			      int reserve);

void vrtx_free_idmap(vrtxidmap_t *map);

int vrtx_get_id(vrtxidmap_t *map,
		int id,
		void *objaddr);

void vrtx_put_id(vrtxidmap_t *map,
		 int id);

static inline void *vrtx_get_object(vrtxidmap_t *map, int id)
{
    if (id < 0 || id >= map->maxids)
	return NULL;

    return map->objarray[id];
}

struct vrtxtask;

int sc_tecreate_inner(struct vrtxtask *task,
		      void (*entry)(void *),
		      int tid,
		      int prio,
		      int mode,
		      u_long user,
		      u_long sys,
		      char *paddr,
		      u_long psize,
		      int *errp);
#ifdef __cplusplus
}
#endif

#endif /* !_XENO_VRTX_DEFS_H */
