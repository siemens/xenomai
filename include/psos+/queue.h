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

#ifndef _psos_queue_h
#define _psos_queue_h

#include "psos+/defs.h"
#include "psos+/psos.h"

#define PSOS_QUEUE_MAGIC 0x81810303

/* These flags are cumulative with standard queue creation flags */
#define Q_VARIABLE   XNSYNCH_SPARE0 /* Variable-size elements */
#define Q_NOCACHE    XNSYNCH_SPARE1 /* No mbuf cache -- use region #0 */
#define Q_PRIVCACHE  XNSYNCH_SPARE2 /* Use queue's private mbuf cache */
#define Q_SHAREDINIT XNSYNCH_SPARE3 /* Init private cache from shared pool */
#define Q_INFINITE   XNSYNCH_SPARE4 /* Infinite queue element count */
#define Q_JAMMED     XNSYNCH_SPARE5 /* Queue is currently jammed */

#define PSOS_QUEUE_MIN_ALLOC  64

typedef struct psosmbuf {

    xnholder_t link;

#define link2psosmbuf(laddr) \
((psosmbuf_t *)(((char *)laddr) - (int)(&((psosmbuf_t *)0)->link)))

    u_long len;

    char data[1];

} psosmbuf_t;

typedef struct psosqueue {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;  /* Link in psosqueueq */

#define link2psosqueue(laddr) \
((psosqueue_t *)(((char *)laddr) - (int)(&((psosqueue_t *)0)->link)))

    xnqueue_t chunkq;	/* Chunks used for the private queue */

    xnsynch_t synchbase;

#define synch2psosqueue(saddr) \
((saddr) ? ((psosqueue_t *)(((char *)(saddr)) - (int)(&((psosqueue_t *)0)->synchbase))) : NULL)

    u_long maxnum,
	   maxlen;

    xnqueue_t inq,	/* Incoming message queue */
	      freeq;	/* Free (cache) message queue */

    char name[5];

} psosqueue_t;

#ifdef __cplusplus
extern "C" {
#endif

void psosqueue_init(void);

void psosqueue_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_psos_queue_h */
