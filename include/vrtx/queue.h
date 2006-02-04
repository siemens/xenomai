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

#ifndef _vrtx_queue_h
#define _vrtx_queue_h

#include "vrtx/defs.h"

#define VRTX_QUEUE_MAGIC 0x82820303

typedef struct vrtxqmsg {
    xnholder_t link;

#define link2vrtxmsg(laddr) \
((vrtxqmsg_t *)(((char *)laddr) - (int)(&((vrtxqmsg_t *)0)->link)))

    char *message;
} vrtxqmsg_t;

typedef struct vrtxqueue {

    unsigned magic;   /* Magic code - must be first */

    xnsynch_t synchbase;

    xnqueue_t messageq;
    
    int maxnum;

} vrtxqueue_t;

#ifdef __cplusplus
extern "C" {
#endif

void vrtxqueue_init(void);

void vrtxqueue_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_vrtx_queue_h */
