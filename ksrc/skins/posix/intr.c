/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "xenomai/posix/internal.h"
#include "xenomai/posix/intr.h"

static xnqueue_t pse51_intrq;

int pse51_intr_attach (struct pse51_interrupt *intr,
		       unsigned irq,
		       xnisr_t isr,
		       xniack_t iack)
{
    int err;
    spl_t s;

    xnintr_init(&intr->intr_base,irq,isr,iack,0);

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    xnsynch_init(&intr->synch_base,XNSYNCH_PRIO);
    intr->pending = 0;
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
    intr->magic = PSE51_INTR_MAGIC;
    inith(&intr->link);
    xnlock_get_irqsave(&nklock, s);
    appendq(&pse51_intrq, &intr->link);
    xnlock_put_irqrestore(&nklock, s);

    err = xnintr_attach(&intr->intr_base,intr);

    if (err)
	pse51_intr_detach(intr);

    return -err;
}

int pse51_intr_detach (struct pse51_interrupt *intr)

{
    int rc = XNSYNCH_DONE;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(intr, PSE51_INTR_MAGIC, struct pse51_interrupt))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    rc = xnsynch_destroy(&intr->synch_base);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
    xnintr_detach(&intr->intr_base);

    xnintr_destroy(&intr->intr_base);

    pse51_mark_deleted(intr);

    removeq(&pse51_intrq, &intr->link);

    if (rc == XNSYNCH_RESCHED)
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pse51_intr_control (struct pse51_interrupt *intr, int cmd)

{
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (!pse51_obj_active(intr, PSE51_INTR_MAGIC, struct pse51_interrupt))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    switch (cmd)
	{
	case PTHREAD_IENABLE:

	    err = xnintr_enable(&intr->intr_base);
	    break;

	case PTHREAD_IDISABLE:

	    err = xnintr_disable(&intr->intr_base);
	    break;

	default:

	    err = EINVAL;
	}

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

void pse51_intr_pkg_init (void)
{
    initq(&pse51_intrq);
}

void pse51_intr_pkg_cleanup (void)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    while ((holder = getheadq(&pse51_intrq)) != NULL)
	pse51_intr_detach(link2intr(holder));

    xnlock_put_irqrestore(&nklock, s);
}
