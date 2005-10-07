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

#include "posix/thread.h"
#include "posix/cancel.h"

typedef void (*cleanup_routine_t) (void *);

typedef struct {
    cleanup_routine_t routine;
    void *arg;
    xnholder_t link;

#define link2cleanup_handler(laddr) \
((cleanup_handler_t *)(((char *)laddr)-(int)(&((cleanup_handler_t *)0)->link)))
} cleanup_handler_t;

int pthread_cancel (pthread_t thread)

{
    int cancel_enabled;
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread)) {
	xnlock_put_irqrestore(&nklock, s);
	return ESRCH;
    }

    cancel_enabled = thread_getcancelstate(thread) == PTHREAD_CANCEL_ENABLE;

    if (cancel_enabled
	&& thread_getcanceltype(thread) == PTHREAD_CANCEL_ASYNCHRONOUS)
        pse51_thread_abort(thread, PTHREAD_CANCELED);
    else
	{
	/* pthread_cancel is not a cancellation point, so
           thread == pthread_self() is not a special case. */

        thread_setcancel(thread);

        if (cancel_enabled)
	    {
            /* Unblock thread, so that it can honor the cancellation request. */
            xnpod_unblock_thread(&thread->threadbase);
            xnpod_schedule();
	    }
	}

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

void pthread_cleanup_push (cleanup_routine_t routine, void *arg)

{
    cleanup_handler_t *handler;
    spl_t s;
    
    if (!routine)
        return;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    /* The allocation is inside the critical section in order to make the
       function async-signal safe, that is in order to avoid leaks if an
       asynchronous cancellation request could occur between the call to
       xnmalloc and xnlock_get_irqsave. */

    xnlock_get_irqsave(&nklock, s);

    handler = xnmalloc(sizeof(*handler));

    if (!handler)
	{
        xnlock_put_irqrestore(&nklock, s);
        return ;
	}

    handler->routine = routine;
    handler->arg = arg;
    inith(&handler->link);

    prependq(thread_cleanups(pse51_current_thread()), &handler->link);

    xnlock_put_irqrestore(&nklock, s);
}

void pthread_cleanup_pop (int execute)

{
    cleanup_handler_t *handler;
    xnholder_t *holder;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    holder = getq(thread_cleanups(pse51_current_thread()));

    if (!holder)
	{
        xnlock_put_irqrestore(&nklock, s);
        return;
	}

    handler = link2cleanup_handler(holder);

    if (execute)
        handler->routine(handler->arg);

    /* Same remark as xnmalloc in pthread_cleanup_push */
    xnfree(handler);

    xnlock_put_irqrestore(&nklock, s);
}

int pthread_setcanceltype (int type, int *oldtype_ptr)

{
    pthread_t cur;
    int oldtype;
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    
    switch (type)
	{
	default:

	    return EINVAL;

	case PTHREAD_CANCEL_DEFERRED:
	case PTHREAD_CANCEL_ASYNCHRONOUS:

	    break;
	}

    cur = pse51_current_thread();
    
    xnlock_get_irqsave(&nklock, s);

    oldtype = thread_getcanceltype(cur);

    thread_setcanceltype(cur, type);

    if (type == PTHREAD_CANCEL_ASYNCHRONOUS
	&& thread_getcancelstate(cur) == PTHREAD_CANCEL_ENABLE)
        thread_cancellation_point(cur);

    if (oldtype_ptr)
        *oldtype_ptr=oldtype;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_setcancelstate (int state, int *oldstate_ptr)

{
    pthread_t cur;
    int oldstate;
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    
    switch (state)
	{
	default:

	    return EINVAL;

	case PTHREAD_CANCEL_ENABLE:
	case PTHREAD_CANCEL_DISABLE:

        break;
	}

    cur = pse51_current_thread();

    xnlock_get_irqsave(&nklock, s);

    oldstate = thread_getcancelstate(cur);
    thread_setcancelstate(cur, state);

    if (state == PTHREAD_CANCEL_ENABLE
	&& thread_getcanceltype(cur) == PTHREAD_CANCEL_ASYNCHRONOUS)
        thread_cancellation_point(cur);
    
    if (oldstate_ptr)
        *oldstate_ptr=oldstate;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

void pthread_testcancel (void)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    xnlock_get_irqsave(&nklock, s);
    thread_cancellation_point(pse51_current_thread());
    xnlock_put_irqrestore(&nklock, s);
}

void pse51_cancel_init_thread (pthread_t thread)

{
    thread_setcancelstate(thread, PTHREAD_CANCEL_ENABLE);
    thread_setcanceltype(thread, PTHREAD_CANCEL_DEFERRED);
    thread_clrcancel(thread);
    initq(thread_cleanups(thread));
}

void pse51_cancel_cleanup_thread (pthread_t thread)

{
    xnholder_t *holder;

    while((holder = getq(thread_cleanups(thread))))
	{
        cleanup_handler_t *handler = link2cleanup_handler(holder);
        handler->routine(handler->arg);
        xnfree(handler);
	}
}

EXPORT_SYMBOL(pthread_cancel);
EXPORT_SYMBOL(pthread_cleanup_push);
EXPORT_SYMBOL(pthread_cleanup_pop);
EXPORT_SYMBOL(pthread_setcancelstate);
EXPORT_SYMBOL(pthread_setcanceltype);
EXPORT_SYMBOL(pthread_testcancel);
