/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

#include "xenomai/vxworks/defs.h"

#define WIND_WD_INITIALIZED XNTIMER_SPARE0

typedef struct wind_wd {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;

#define link2wind_wd(laddr) \
((wind_wd_t *)(((char *)laddr) - (int)(&((wind_wd_t *)0)->link)))

    xntimer_t timerbase;

} wind_wd_t;

typedef void (* xntimer_handler) (void *);


static xnqueue_t wind_wd_q;


static void wd_destroy_internal(wind_wd_t *wd);




void wind_wd_init (void)
{
    initq(&wind_wd_q);
}


void wind_wd_cleanup (void)
{
    xnholder_t *holder;

    while ((holder = getheadq(&wind_wd_q)) != NULL)
	wd_destroy_internal(link2wind_wd(holder));
}




WDOG_ID wdCreate (void)
{
    wind_wd_t *wd;
    spl_t s;

    check_alloc(wind_wd_t, wd, return 0);

    inith(&wd->link);
    wd->magic = WIND_WD_MAGIC;

    setbits(wd->timerbase.status, WIND_WD_INITIALIZED);
    
    xnlock_get_irqsave(&nklock, s);
    appendq(&wind_wd_q,&wd->link);
    xnlock_put_irqrestore(&nklock, s);

    return (WDOG_ID) wd;
}



STATUS wdDelete (WDOG_ID handle)
{
    wind_wd_t *wd;
    spl_t s;

    /*    xnpod_check_context(XNPOD_THREAD_CONTEXT); */

    xnlock_get_irqsave(&nklock, s);
    check_OBJ_ID_ERROR(handle, wind_wd_t, wd, WIND_WD_MAGIC, goto error);
    wd_destroy_internal(wd);
    xnlock_put_irqrestore(&nklock, s);
    return OK;

 error:
    xnlock_put_irqrestore(&nklock, s);
    return ERROR;
}



STATUS wdStart ( WDOG_ID handle,
                 int timeout,
                 wind_timer_t handler,
                 int arg )
{
    wind_wd_t *wd;
    spl_t s;

    if (!handler)
	return ERROR;

    xnlock_get_irqsave(&nklock, s);

    check_OBJ_ID_ERROR(handle, wind_wd_t, wd, WIND_WD_MAGIC, goto error);

    if(testbits(wd->timerbase.status, WIND_WD_INITIALIZED))
        clrbits(wd->timerbase.status, WIND_WD_INITIALIZED);
    else
        if(xntimer_running_p(&wd->timerbase))
            xntimer_stop(&wd->timerbase);
    
    xntimer_init(&wd->timerbase, (xntimer_handler) handler, (void *) (long)arg);
    
    xntimer_start(&wd->timerbase,timeout,XN_INFINITE);

    xnlock_put_irqrestore(&nklock, s);
    return OK;

 error:
    xnlock_put_irqrestore(&nklock, s);
    return ERROR;
}



STATUS wdCancel ( WDOG_ID handle )
{
    wind_wd_t *wd;
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);
    check_OBJ_ID_ERROR(handle, wind_wd_t, wd, WIND_WD_MAGIC, goto error);
    if(xntimer_running_p(&wd->timerbase))
        xntimer_stop(&wd->timerbase);
    xnlock_put_irqrestore(&nklock, s);

    return OK;

 error:
    xnlock_put_irqrestore(&nklock, s);
    return ERROR;
}




static void wd_destroy_internal (wind_wd_t * handle)
{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);
    if(testbits(handle->timerbase.status, XNTIMER_ENABLED))
        xntimer_destroy(&handle->timerbase);
    removeq(&wind_wd_q,&handle->link);
    wind_mark_deleted(handle);
    xnlock_put_irqrestore(&nklock, s);

    xnfree(handle);
}
