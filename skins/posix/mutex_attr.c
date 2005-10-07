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

static const pthread_mutexattr_t default_mutex_attr = {
    magic: PSE51_MUTEX_ATTR_MAGIC,
    type: PTHREAD_MUTEX_NORMAL,
    protocol: PTHREAD_PRIO_NONE
};

int pthread_mutexattr_init (pthread_mutexattr_t * attr)

{
    if (!attr)
        return ENOMEM;

    *attr = default_mutex_attr;

    return 0;    
}

int pthread_mutexattr_destroy (pthread_mutexattr_t *attr)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}
    
    pse51_mark_deleted(attr);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_mutexattr_gettype (const pthread_mutexattr_t *attr, int *type)

{
    spl_t s;

    if (!type)
        return EINVAL;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *type = attr->type;

    xnlock_put_irqrestore(&nklock, s);

    return 0;    
}

int pthread_mutexattr_settype (pthread_mutexattr_t *attr, int type)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    switch (type)
	{
	default:

	    xnlock_put_irqrestore(&nklock, s);
	    return EINVAL;

	case PTHREAD_MUTEX_DEFAULT:
	    type = PTHREAD_MUTEX_NORMAL;

	case PTHREAD_MUTEX_NORMAL:
	case PTHREAD_MUTEX_RECURSIVE:
	case PTHREAD_MUTEX_ERRORCHECK:
	    break;
    }
    
    attr->type = type;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_mutexattr_getprotocol (const pthread_mutexattr_t *attr, int *proto)

{
    spl_t s;

    if (!proto)
        return EINVAL;
    
    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *proto = attr->protocol;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}


int pthread_mutexattr_setprotocol (pthread_mutexattr_t *attr, int proto)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    switch (proto)
	{
	default:

	    xnlock_put_irqrestore(&nklock, s);
	    return EINVAL;

	case PTHREAD_PRIO_PROTECT:

	    xnlock_put_irqrestore(&nklock, s);
	    return ENOTSUP;

	case PTHREAD_PRIO_NONE:
	case PTHREAD_PRIO_INHERIT:
	    break;
	}
    
    attr->protocol = proto;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

EXPORT_SYMBOL(pthread_mutexattr_init);
EXPORT_SYMBOL(pthread_mutexattr_destroy);
EXPORT_SYMBOL(pthread_mutexattr_gettype);
EXPORT_SYMBOL(pthread_mutexattr_settype);
EXPORT_SYMBOL(pthread_mutexattr_getprotocol);
EXPORT_SYMBOL(pthread_mutexattr_setprotocol);
