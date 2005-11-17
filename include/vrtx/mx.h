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

#ifndef _vrtx_mx_h
#define _vrtx_mx_h

#include "xenomai/vrtx/defs.h"

#define VRTXMX_FREE      0
#define VRTXMX_LOCKED    1
#define VRTXMX_UNLOCKED  2

typedef struct vrtxmx {
    xnholder_t link;

#define link2vrtxmx(laddr) \
((vrtxmx_t *)(((char *)laddr) - (int)(&((vrtxmx_t *)0)->link)))

    int state;

    xnthread_t *owner;

    xnsynch_t synchbase;
} vrtxmx_t;


#ifdef __cplusplus
extern "C" {
#endif

void vrtxmx_init(void);

void vrtxmx_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_vrtx_mx_h */
