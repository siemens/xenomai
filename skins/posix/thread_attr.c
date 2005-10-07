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

static const pthread_attr_t default_thread_attr = {
    magic: PSE51_THREAD_ATTR_MAGIC,
    detachstate: PTHREAD_CREATE_JOINABLE,
    stacksize: PTHREAD_STACK_MIN,
    inheritsched: PTHREAD_EXPLICIT_SCHED,
    policy: SCHED_FIFO,
    schedparam: {
        sched_priority: PSE51_MIN_PRIORITY
    },

    name: NULL,
    fp: 1,
    affinity: XNPOD_ALL_CPUS,
};

int pthread_attr_init (pthread_attr_t *attr)

{
    if (!attr)
        return ENOMEM;

    *attr = default_thread_attr;

    return 0;
}

int pthread_attr_destroy (pthread_attr_t *attr)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    if (attr->name)
        xnfree(attr->name);

    pse51_mark_deleted(attr);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_getdetachstate (const pthread_attr_t *attr, int *detachstate)

{
    spl_t s;

    if (!detachstate)
        return EINVAL;
        
    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *detachstate = attr->detachstate;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_setdetachstate (pthread_attr_t *attr, int detachstate)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    attr->detachstate = detachstate;
    xnlock_put_irqrestore(&nklock, s);
       
    return 0;
}

int pthread_attr_getstackaddr (const pthread_attr_t *attr, void **stackaddr)

{
    spl_t s;

    if (!stackaddr)
        return EINVAL;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    xnlock_put_irqrestore(&nklock, s);

    return ENOSYS;
}

int pthread_attr_setstackaddr (pthread_attr_t *attr, void *stackaddr)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    xnlock_put_irqrestore(&nklock, s);

    return ENOSYS;
}

int pthread_attr_getstacksize (const pthread_attr_t *attr, size_t *stacksize)

{
    spl_t s;

    if (!stacksize)
        return EINVAL;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *stacksize = attr->stacksize;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_setstacksize (pthread_attr_t *attr, size_t stacksize)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    if (stacksize < PTHREAD_STACK_MIN)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    attr->stacksize = stacksize;
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_getinheritsched (const pthread_attr_t *attr,int *inheritsched)

{
    spl_t s;

    if (!inheritsched)
        return EINVAL;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *inheritsched = attr->inheritsched;

    return 0;
}

int pthread_attr_setinheritsched (pthread_attr_t *attr, int inheritsched)

{
    spl_t s;

    switch (inheritsched)
	{
	default:
	    return EINVAL;

	case PTHREAD_INHERIT_SCHED:
	case PTHREAD_EXPLICIT_SCHED:
	    break;
	}

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    attr->inheritsched = inheritsched;

    xnlock_put_irqrestore(&nklock, s);
    
    return 0;
}

int pthread_attr_getschedpolicy (const pthread_attr_t *attr,int *policy)

{
    spl_t s;

    if (!policy)
        return EINVAL;
    
    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *policy = attr->policy;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_setschedpolicy (pthread_attr_t *attr, int policy)

{
    spl_t s;

    switch (policy)
	{
	default:

	    return EINVAL;

	case SCHED_OTHER:
	    policy = SCHED_RR;

	case SCHED_FIFO:
	case SCHED_RR:

	    break;
	}

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    attr->policy = policy;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_getschedparam (const pthread_attr_t *attr, struct sched_param *par)

{
    spl_t s;

    if (!par)
        return EINVAL;
    
    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *par = attr->schedparam;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_setschedparam (pthread_attr_t *attr, const struct sched_param *par)

{
    spl_t s;

    if (!par)
        return EINVAL;

    if (par->sched_priority < PSE51_MIN_PRIORITY
	|| par->sched_priority > PSE51_MAX_PRIORITY )
        return EINVAL;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    attr->schedparam = *par;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_getscope (const pthread_attr_t *attr,int *scope)

{
    spl_t s;

    if (!scope)
        return EINVAL;
    
    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *scope = PTHREAD_SCOPE_SYSTEM;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_setscope (pthread_attr_t *attr,int scope)

{
    spl_t s;

    if (scope != PTHREAD_SCOPE_SYSTEM)
        return ENOTSUP;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_getname_np (const pthread_attr_t *attr, const char **name)

{
    spl_t s;

    if (!name)
        return EINVAL;
    
    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *name = attr->name;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_setname_np (pthread_attr_t *attr, const char *name)

{
    spl_t s;

    if (!name)
        return EINVAL;
    
    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    if (attr->name)
        xnfree(attr->name);

    if ((attr->name = xnmalloc(strlen(name)+1)))
        strcpy(attr->name, name);
    
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_getfp_np (const pthread_attr_t *attr, int *fp)

{
    spl_t s;

    if (!fp)
        return EINVAL;
    
    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *fp = attr->fp;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_setfp_np (pthread_attr_t *attr, int fp)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    attr->fp = fp;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int
pthread_attr_getaffinity_np (const pthread_attr_t *attr, xnarch_cpumask_t *mask)

{
    spl_t s;

    if (!mask)
        return EINVAL;
    
    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    *mask = attr->affinity;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_attr_setaffinity_np (pthread_attr_t *attr, xnarch_cpumask_t mask)
{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    attr->affinity = mask;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

EXPORT_SYMBOL(pthread_attr_init);
EXPORT_SYMBOL(pthread_attr_destroy);
EXPORT_SYMBOL(pthread_attr_getdetachstate);
EXPORT_SYMBOL(pthread_attr_setdetachstate);
EXPORT_SYMBOL(pthread_attr_getstackaddr);
EXPORT_SYMBOL(pthread_attr_setstackaddr);
EXPORT_SYMBOL(pthread_attr_getstacksize);
EXPORT_SYMBOL(pthread_attr_setstacksize);
EXPORT_SYMBOL(pthread_attr_getinheritsched);
EXPORT_SYMBOL(pthread_attr_setinheritsched);
EXPORT_SYMBOL(pthread_attr_getschedpolicy);
EXPORT_SYMBOL(pthread_attr_setschedpolicy);
EXPORT_SYMBOL(pthread_attr_getschedparam);
EXPORT_SYMBOL(pthread_attr_setschedparam);
EXPORT_SYMBOL(pthread_attr_getscope);
EXPORT_SYMBOL(pthread_attr_setscope);
EXPORT_SYMBOL(pthread_attr_getname_np);
EXPORT_SYMBOL(pthread_attr_setname_np);
EXPORT_SYMBOL(pthread_attr_getfp_np);
EXPORT_SYMBOL(pthread_attr_setfp_np);
EXPORT_SYMBOL(pthread_attr_getaffinity_np );
EXPORT_SYMBOL(pthread_attr_setaffinity_np );
