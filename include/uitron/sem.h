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

#ifndef _uITRON_sem_h
#define _uITRON_sem_h

#include "xenomai/uitron/defs.h"

#define uITRON_SEM_MAGIC 0x85850202

typedef struct uisem {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;	/* Link in uisemq */

#define link2uisem(laddr) \
((uisem_t *)(((char *)laddr) - (int)(&((uisem_t *)0)->link)))

    ID semid;

    VP exinf;

    ATR sematr;

    INT semcnt;

    INT maxsem;

    xnsynch_t synchbase;

} uisem_t;

#ifdef __cplusplus
extern "C" {
#endif

void uisem_init(void);

void uisem_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_uITRON_sem_h */
