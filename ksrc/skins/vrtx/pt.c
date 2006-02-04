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

#include "vrtx/pt.h"

static xnqueue_t vrtxptq;

static vrtxpt_t *vrtxptmap[VRTX_MAX_PID];

static void vrtxpt_delete_internal(vrtxpt_t *pt);

void vrtxpt_init (void) {
    initq(&vrtxptq);
}

void vrtxpt_cleanup (void) {

    xnholder_t *holder;

    while ((holder = getheadq(&vrtxptq)) != NULL)
	vrtxpt_delete_internal(link2vrtxpt(holder));
}

static void vrtxpt_delete_internal (vrtxpt_t *pt)

{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);
    removeq(&vrtxptq,&pt->link);
    vrtxptmap[pt->pid] = NULL;
    vrtx_mark_deleted(pt);
    xnlock_put_irqrestore(&nklock,s);
}

static int vrtxpt_add_extent (vrtxpt_t *pt,
			      char *extaddr,
			      long extsize)
{
    u_long bitmapsize;
    vrtxptext_t *ptext;
    char *mp;
    long n;
    spl_t s;

    if (extsize <= pt->bsize + sizeof(vrtxptext_t))
	return ER_IIP;

    extsize -= sizeof(vrtxptext_t);
    ptext = (vrtxptext_t *)extaddr;
    inith(&ptext->link);

    bitmapsize = (extsize * 8) / (pt->bsize + 8);
    bitmapsize = (bitmapsize + ptext_align_mask) & ~ptext_align_mask;

    if (bitmapsize <= ptext_align_mask)
	return ER_IIP;

    ptext->nblks = (extsize - bitmapsize) / pt->bsize;

    if (ptext->nblks > 65534)
	return ER_IIP;

    ptext->extsize = ptext->nblks * pt->bsize;
    ptext->data = (char *)ptext->bitmap + bitmapsize;
    ptext->freelist = mp = ptext->data;

    pt->fblks += ptext->nblks;

    for (n = ptext->nblks; n > 1; n--)
	{
	char *nmp = mp + pt->bsize;
	*((void **)mp) = nmp;
	mp = nmp;
	}

    *((void **)mp) = NULL;

    for (n = bitmapsize / sizeof(u_long) - 1; n >= 0; n--)
	ptext->bitmap[n] = 0;
 
    xnlock_get_irqsave(&nklock,s);
    appendq(&pt->extq,&ptext->link);
    xnlock_put_irqrestore(&nklock,s);

    return RET_OK;
}

int sc_pcreate (int pid,
		char *paddr,
		long psize,
		long bsize,
		int *perr)
{
    vrtxpt_t *pt;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (pid < -1 || pid >= VRTX_MAX_PID ||
	bsize <= ptext_align_mask ||
	psize < bsize + sizeof(vrtxpt_t) + sizeof(vrtxptext_t))
	{
	*perr = ER_IIP;
	return -1;
	}

    xnlock_get_irqsave(&nklock,s);

    if (pid < 0)
	{
	for (pid = 0; pid < VRTX_MAX_PID; pid++)
	    {
	    if (vrtxptmap[pid] == NULL)
		break;
	    }

	if (pid >= VRTX_MAX_PID)
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    *perr = ER_PID;
	    return -1;
	    }
	}
    else if (pid >= 0 && vrtxptmap[pid] != NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*perr = ER_PID;
	return -1;
	}

    /* Reserve slot while preemption is disabled */
    vrtxptmap[pid] = (vrtxpt_t *)1;

    xnlock_put_irqrestore(&nklock,s);

    pt = (vrtxpt_t *)paddr;
    inith(&pt->link);
    initq(&pt->extq);
    pt->bsize = (bsize + ptext_align_mask) & ~ptext_align_mask;
    pt->ublks = 0;
    pt->pid = pid;

    *perr = vrtxpt_add_extent(pt,
			      (char *)pt + sizeof(*pt),
			      psize - sizeof(*pt));
    if (*perr != RET_OK)
	{
	vrtxptmap[pid] = NULL;
	return -1;
	}
    
    pt->magic = VRTX_PT_MAGIC;
    xnlock_get_irqsave(&nklock,s);
    vrtxptmap[pid] = pt;
    appendq(&vrtxptq,&pt->link);
    xnlock_put_irqrestore(&nklock,s);

    return pid;
}

void sc_pdelete (int pid, int opt, int *perr)

{
    vrtxpt_t *pt;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (pid < 0 || pid >= VRTX_MAX_PID)
	{
	*perr = ER_PID;
	return;
	}

    if ((opt & ~1) != 0)
	{
	*perr = ER_IIP;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    pt = vrtxptmap[pid];

    if (pt == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*perr = ER_PID;
	return;
	}

    vrtxpt_delete_internal(pt);

    *perr = RET_OK;
    
    xnlock_put_irqrestore(&nklock,s);
}

char *sc_gblock (int pid, int *perr)

{
    vrtxptext_t *ptext;
    xnholder_t *holder;
    u_long numblk;
    vrtxpt_t *pt;
    void *buf;
    spl_t s;

    if (pid < 0 || pid >= VRTX_MAX_PID)
	{
	*perr = ER_PID;
	return NULL;
	}

    xnlock_get_irqsave(&nklock,s);

    pt = vrtxptmap[pid];

    if (pt == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*perr = ER_PID;
	return NULL;
	}

    for (holder = getheadq(&pt->extq), buf = NULL;
	holder; holder = nextq(&pt->extq,holder))
	{
	ptext = link2vrtxptext(holder);

	if ((buf = ptext->freelist) != NULL)
	    {
	    ptext->freelist = *((void **)buf);
	    pt->ublks++;
	    pt->fblks--;
	    numblk = ((char *)buf - ptext->data) / pt->bsize;
	    ptext_bitmap_setbit(ptext,numblk);
	    break;
	    }
	}

    xnlock_put_irqrestore(&nklock,s);

    *perr = (buf == NULL ? ER_MEM : RET_OK);

    return (char *)buf;
}

void sc_rblock (int pid, char *buf, int *perr)

{
    vrtxptext_t *ptext;
    xnholder_t *holder;
    u_long numblk;
    vrtxpt_t *pt;
    spl_t s;

    if (pid < 0 || pid >= VRTX_MAX_PID)
	{
	*perr = ER_PID;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    pt = vrtxptmap[pid];

    if (pt == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*perr = ER_PID;
	return;
	}

    /* For each extent linked to the partition's queue */

    for (holder = getheadq(&pt->extq);
	holder; holder = nextq(&pt->extq,holder))
	{
	ptext = link2vrtxptext(holder);

	/* Check if the released buffer address lays into the
	   currently scanned extent. */

	if (buf >= ptext->data &&
	    buf < ptext->data + ptext->extsize)
	    {
	    if (((buf - ptext->data) % pt->bsize) != 0)
		goto nmb;

	    numblk = (buf - ptext->data) / pt->bsize;

	    /* Check using the bitmap if the block
	       was previously allocated. Remember that
	       gblock()/rblock() ops are valid on behalf of
	       ISRs, so we need to protect ourselves using a hard
	       critical section.*/

	    if (ptext_bitmap_tstbit(ptext,numblk))
		{
		/* Ok, all is fine: release and exit */
		ptext_bitmap_clrbit(ptext,numblk);
		*((void **)buf) = ptext->freelist;
		ptext->freelist = buf;
		pt->ublks--;
		pt->fblks++;
		xnlock_put_irqrestore(&nklock,s);
		*perr = RET_OK;
		return;
		}
	    }
	}

nmb:

    xnlock_put_irqrestore(&nklock,s);

    *perr = ER_NMB;
}

void sc_pextend (int pid,
		 char *extaddr,
		 long extsize,
		 int *perr)
{
    vrtxpt_t *pt;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (pid < 0 || pid >= VRTX_MAX_PID)
	{
	*perr = ER_PID;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    pt = vrtxptmap[pid];

    if (pt == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*perr = ER_PID;
	return;
	}

    *perr = vrtxpt_add_extent(pt,extaddr,extsize);

    xnlock_put_irqrestore(&nklock,s);
}

void sc_pinquiry (unsigned long info[3], int pid, int *errp)

{
    vrtxpt_t *pt;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (pid < 0 || pid >= VRTX_MAX_PID)
	{
	*errp = ER_PID;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    pt = vrtxptmap[pid];

    if (pt == NULL)
	{
	xnlock_put_irqrestore(&nklock,s);
	*errp = ER_PID;
	return;
	}

    info[0] = pt->ublks;
    info[1] = pt->fblks;
    info[2] = pt->bsize;

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;

}
/*
 * IMPLEMENTATION NOTES:
 *
 * - A partition memory layout is as follows:
 *
 *   struct vrtxpt {
 *      Partition's superblock
 *      Extent queue (vrtxptext) -----+
 *   }                                |
 *                                    |
 *                                    |
 *                                    |
 *   struct vrtxext { <---------------+ x N
 *
 *      (char *data => pointer to the user data area)
 *      (u_long bitmap[1] => first word of bitmap)
 *
 *   }
 *   [...block status bitmap (busy/free)...]
 *   [...user data area...]
 *
 * - Each free block starts with a link to the next free block
 * in the partition's free list. A NULL link ends this list.
 */
