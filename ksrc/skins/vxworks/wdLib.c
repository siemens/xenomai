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

#include <vxworks/defs.h>

#define WIND_WD_INITIALIZED XNTIMER_SPARE0

static xnqueue_t wind_wd_q;

static void wd_destroy_internal(wind_wd_t *wd);

#ifdef CONFIG_XENO_EXPORT_REGISTRY

static int wd_read_proc(char *page,
			char **start,
			off_t off, int count, int *eof, void *data)
{
	wind_wd_t *wd = (wind_wd_t *)data;
	char *p = page;
	int len;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	p += sprintf(p, "timeout=%lld\n", xntimer_get_timeout(&wd->timerbase));

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	{
		xnpholder_t *holder =
		    getheadpq(xnsynch_wait_queue(&wd->synchbase));

		while (holder) {
			xnthread_t *sleeper = link2thread(holder, plink);
			p += sprintf(p, "+%s\n", xnthread_name(sleeper));
			holder =
			    nextpq(xnsynch_wait_queue(&wd->synchbase), holder);
		}
	}
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

	xnlock_put_irqrestore(&nklock, s);

	len = (p - page) - off;
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

extern xnptree_t __vxworks_ptree;

static xnpnode_t wd_pnode = {

	.dir = NULL,
	.type = "watchdogs",
	.entries = 0,
	.read_proc = &wd_read_proc,
	.write_proc = NULL,
	.root = &__vxworks_ptree,
};

#elif defined(CONFIG_XENO_OPT_REGISTRY)

static xnpnode_t wd_pnode = {

	.type = "watchdogs"
};

#endif /* CONFIG_XENO_EXPORT_REGISTRY */

static void wind_wd_trampoline(xntimer_t *timer)
{
	wind_wd_t *wd = container_of(timer, wind_wd_t, timerbase);

	wd->handler(wd->arg);
}

void wind_wd_init(void)
{
	initq(&wind_wd_q);
}

void wind_wd_cleanup(void)
{
	xnholder_t *holder;

	while ((holder = getheadq(&wind_wd_q)) != NULL)
		wd_destroy_internal(link2wind_wd(holder));
}

WDOG_ID wdCreate(void)
{
	wind_wd_t *wd;
	spl_t s;

	check_alloc(wind_wd_t, wd, return 0);

	inith(&wd->link);
	wd->magic = WIND_WD_MAGIC;
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	xnsynch_init(&wd->synchbase, XNSYNCH_PRIO);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

	xnlock_get_irqsave(&nklock, s);
	__setbits(wd->timerbase.status, WIND_WD_INITIALIZED);
	appendq(&wind_wd_q, &wd->link);
	xnlock_put_irqrestore(&nklock, s);

#ifdef CONFIG_XENO_OPT_REGISTRY
	{
		static unsigned long wd_ids;

		sprintf(wd->name, "wd%lu", wd_ids++);

		if (xnregistry_enter(wd->name, wd, &wd->handle, &wd_pnode)) {
			wind_errnoset(S_objLib_OBJ_ID_ERROR);
			wdDelete((WDOG_ID)wd);
			return 0;
		}
	}
#endif /* CONFIG_XENO_OPT_REGISTRY */

	return (WDOG_ID)wd;
}

STATUS wdDelete(WDOG_ID wdog_id)
{
	wind_wd_t *wd;
	spl_t s;

	error_check(xnpod_asynch_p(), -EPERM, return ERROR);
	xnlock_get_irqsave(&nklock, s);
	check_OBJ_ID_ERROR(wdog_id, wind_wd_t, wd, WIND_WD_MAGIC, goto error);
	wd_destroy_internal(wd);
	xnlock_put_irqrestore(&nklock, s);
	return OK;

      error:
	xnlock_put_irqrestore(&nklock, s);
	return ERROR;
}

STATUS wdStart(WDOG_ID wdog_id, int timeout, wind_timer_t handler, long arg)
{
	wind_wd_t *wd;
	spl_t s;

	if (!handler)
		return ERROR;

	xnlock_get_irqsave(&nklock, s);

	check_OBJ_ID_ERROR(wdog_id, wind_wd_t, wd, WIND_WD_MAGIC, goto error);

	if (testbits(wd->timerbase.status, WIND_WD_INITIALIZED))
		__clrbits(wd->timerbase.status, WIND_WD_INITIALIZED);
	else
		xntimer_stop(&wd->timerbase);

	xntimer_init(&wd->timerbase, wind_wd_trampoline);
	wd->handler = handler;
	wd->arg = arg;

	xntimer_start(&wd->timerbase, timeout, XN_INFINITE, XNTIMER_RELATIVE);

	xnlock_put_irqrestore(&nklock, s);
	return OK;

      error:
	xnlock_put_irqrestore(&nklock, s);
	return ERROR;
}

STATUS wdCancel(WDOG_ID wdog_id)
{
	wind_wd_t *wd;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	check_OBJ_ID_ERROR(wdog_id, wind_wd_t, wd, WIND_WD_MAGIC, goto error);
	xntimer_stop(&wd->timerbase);
	xnlock_put_irqrestore(&nklock, s);

	return OK;

      error:
	xnlock_put_irqrestore(&nklock, s);
	return ERROR;
}

static void wd_destroy_internal(wind_wd_t *wd)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	xntimer_destroy(&wd->timerbase);
#ifdef CONFIG_XENO_OPT_REGISTRY
	xnregistry_remove(wd->handle);
#endif /* CONFIG_XENO_OPT_REGISTRY */
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	xnsynch_destroy(&wd->synchbase);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
	removeq(&wind_wd_q, &wd->link);
	wind_mark_deleted(wd);
	xnlock_put_irqrestore(&nklock, s);

	xnfree(wd);
}
