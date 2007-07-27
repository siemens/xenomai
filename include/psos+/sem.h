/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _psos_sem_h
#define _psos_sem_h

#include "psos+/defs.h"
#include "psos+/psos.h"

#define PSOS_SEM_MAGIC 0x81810202

typedef struct psossem {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;  /* Link in psossemq */

#define link2psossem(ln) container_of(ln, psossem_t, link)

    char name[XNOBJECT_NAME_LEN]; /* Name of semaphore */

#ifdef CONFIG_XENO_OPT_REGISTRY
    xnhandle_t handle;
#endif /* CONFIG_XENO_OPT_REGISTRY */

    xnsynch_t synchbase;

    unsigned count;   /* Available resource count */

} psossem_t;

#ifdef __cplusplus
extern "C" {
#endif

void psossem_init(void);

void psossem_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_psos_sem_h */
