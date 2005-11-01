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

#ifndef _uITRON_mbx_h
#define _uITRON_mbx_h

#include "xenomai/uitron/defs.h"

#define uITRON_MBX_MAGIC 0x85850404

typedef struct uimbx {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;	/* Link in uimbxq */

#define link2uimbx(laddr) \
((uimbx_t *)(((char *)laddr) - (int)(&((uimbx_t *)0)->link)))

    ID mbxid;

    VP exinf;

    ATR mbxatr;

    INT bufcnt;

    UINT rdptr;

    UINT wrptr;

    UINT mcount;

    T_MSG **ring;

    xnsynch_t synchbase;

} uimbx_t;

#ifdef __cplusplus
extern "C" {
#endif

void uimbx_init(void);

void uimbx_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_uITRON_mbx_h */
