/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 *
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

#include "posix/internal.h"

static pthread_condattr_t default_cond_attr = {

    magic: PSE51_COND_ATTR_MAGIC,
    clock: CLOCK_REALTIME
};


int pthread_condattr_init (pthread_condattr_t *attr)

{
    if (!attr)
        return ENOMEM;

    *attr = default_cond_attr;

    return 0;
}

int pthread_condattr_destroy (pthread_condattr_t *attr)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_COND_ATTR_MAGIC, pthread_condattr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}
    
    pse51_mark_deleted(attr);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_condattr_getclock (const pthread_condattr_t *attr, clockid_t *clk_id)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_COND_ATTR_MAGIC, pthread_condattr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *clk_id = attr->clock;
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_condattr_setclock (pthread_condattr_t *attr, clockid_t clk_id)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_COND_ATTR_MAGIC, pthread_condattr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    switch (clk_id)
	{
	default:

	    xnlock_put_irqrestore(&nklock, s);
	    return EINVAL;

	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
	    break;
	}

    attr->clock = clk_id;
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

EXPORT_SYMBOL(pthread_condattr_init);
EXPORT_SYMBOL(pthread_condattr_destroy);
EXPORT_SYMBOL(pthread_condattr_getclock);
EXPORT_SYMBOL(pthread_condattr_setclock);
