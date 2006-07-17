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

#ifndef _uITRON_task_h
#define _uITRON_task_h

#include "uitron/defs.h"

#define uITRON_TASK_MAGIC 0x85850101

#define uITRON_TERM_HOLD  XNTHREAD_SPARE0
#define uITRON_TASK_SLEEP XNTHREAD_SPARE1

typedef struct uitask {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;	/* Link in uitaskq */

#define link2uitask(laddr) \
((uitask_t *)(((char *)laddr) - (int)(&((uitask_t *)0)->link)))

#define thread2uitask(taddr) \
((taddr) ? ((uitask_t *)(((char *)(taddr)) - (int)(&((uitask_t *)0)->threadbase))) : NULL)

    ID tskid;

    FPTR entry;

    INT stacd;

    VP exinf;

    ATR tskatr;		/* Not used */

    int suspcnt;	/* Suspend count */

    int wkupcnt;	/* Wakeup count */

    int waitinfo;	/* Cause of wait */

    union {

	struct {
	    UINT waiptn;
	    UINT wfmode;
	} flag;

	T_MSG *msg;

	struct {
	    VP msgptr;
	    INT msgsz;
	} mbuf;

    } wargs;		/* Wait channel args */

    xnthread_t threadbase;

} uitask_t;

#define ui_current_task() thread2uitask(xnpod_current_thread())

#ifdef __cplusplus
extern "C" {
#endif

void uitask_init(void);

void uitask_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_uITRON_task_h */
