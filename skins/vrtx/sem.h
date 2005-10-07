/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _vrtx_sem_h
#define _vrtx_sem_h

#include "vrtx/defs.h"

#define VRTX_SEM_MAGIC 0x82820202
#define MAX_SEM_VALUE  65535


typedef struct vrtxsem {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;  /* Link in vrtxsemq */

#define link2vrtxsem(laddr) \
((vrtxsem_t *)(((char *)laddr) - (int)(&((vrtxsem_t *)0)->link)))

    int semid;		/* VRTX identifier */

    xnsynch_t synchbase;

    u_long count;   /* Available resource count */

} vrtxsem_t;

#ifdef __cplusplus
extern "C" {
#endif

void vrtxsem_init(void);

void vrtxsem_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_vrtx_sem_h */
