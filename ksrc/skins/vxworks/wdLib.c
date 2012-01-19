/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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

static void wd_destroy_internal(wind_wd_t *wd);

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	xnticks_t timeout;
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	wind_wd_t *wd = xnvfile_priv(it->vfile);
	int nr;

	wd = wind_h2obj_active((WDOG_ID)wd, WIND_WD_MAGIC, wind_wd_t);
	if (wd == NULL)
		return -EIDRM;

#ifdef CONFIG_XENO_OPT_PERVASIVE
	priv->curr = getheadpq(xnsynch_wait_queue(&wd->rh->wdsynch));
	nr = xnsynch_nsleepers(&wd->rh->wdsynch);
#else
	priv->curr = NULL;
	nr = 0;
#endif
	priv->timeout = xntimer_get_timeout(&wd->timerbase);

	return nr;
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
#ifdef CONFIG_XENO_OPT_PERVASIVE
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	wind_wd_t *wd = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	/* Refresh as we collect. */
	priv->timeout = xntimer_get_timeout(&wd->timerbase);

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&wd->rh->wdsynch),
			    priv->curr);
	/* Collect thread name to be output in ->show(). */
	strncpy(p->name, xnthread_name(thread), sizeof(p->name));

	return 1;
#else
	return 0;
#endif
}

static int vfile_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vfile_data *p = data;

	if (p == NULL) {	/* Dump header. */
		xnvfile_printf(it, "timeout=%Lu\n", priv->timeout);
		if (it->nrdata > 0)
			/* Watchdog is pended -- dump waiters */
			xnvfile_printf(it, "-------------------------------------------\n");
	} else
		xnvfile_printf(it, "%.*s\n",
			       (int)sizeof(p->name), p->name);

	return 0;
}

static struct xnvfile_snapshot_ops vfile_ops = {
	.rewind = vfile_rewind,
	.next = vfile_next,
	.show = vfile_show,
};

extern struct xnptree __vxworks_ptree;

static struct xnpnode_snapshot __wd_pnode = {
	.node = {
		.dirname = "watchdogs",
		.root = &__vxworks_ptree,
		.ops = &xnregistry_vfsnap_ops,
	},
	.vfile = {
		.privsz = sizeof(struct vfile_priv),
		.datasz = sizeof(struct vfile_data),
		.ops = &vfile_ops,
	},
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_snapshot __wd_pnode = {
	.node = {
		.dirname = "watchdogs",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

static void wind_wd_trampoline(xntimer_t *timer)
{
	wind_wd_t *wd = container_of(timer, wind_wd_t, timerbase);

	wd->handler(wd->arg);
}

void wind_wd_init(void)
{
}

void wind_wd_cleanup(void)
{
	wind_wd_flush_rq(&__wind_global_rholder.wdq);
}

WDOG_ID wdCreate(void)
{
	static unsigned long wd_ids;
	wind_wd_t *wd;
	spl_t s;

	check_alloc(wind_wd_t, wd, return 0);

	wd->magic = WIND_WD_MAGIC;
#ifdef CONFIG_XENO_OPT_PERVASIVE
	wd->rh = wind_get_rholder();
	inith(&wd->plink);
#endif /* CONFIG_XENO_OPT_PERVASIVE */

	xntimer_init(&wd->timerbase, wind_tbase, wind_wd_trampoline);

	inith(&wd->rlink);
	wd->rqueue = &wind_get_rholder()->wdq;
	xnlock_get_irqsave(&nklock, s);
	__setbits(wd->timerbase.status, WIND_WD_INITIALIZED);
	appendq(wd->rqueue, &wd->rlink);
	xnlock_put_irqrestore(&nklock, s);

	sprintf(wd->name, "wd%lu", wd_ids++);

	if (xnregistry_enter(wd->name, wd, &wd->handle, &__wd_pnode.node)) {
		wind_errnoset(S_objLib_OBJ_ID_ERROR);
		wdDelete((WDOG_ID)wd);
		return 0;
	}

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
	xnfree(wd);
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

	wd->handler = handler;
	wd->arg = arg;

	xntimer_start(&wd->timerbase, timeout, XN_INFINITE, XN_RELATIVE);

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

/* Called with nklock locked, interrupts off. */
static void wd_destroy_internal(wind_wd_t *wd)
{
	removeq(wd->rqueue, &wd->rlink);
	xntimer_destroy(&wd->timerbase);
	xnregistry_remove(wd->handle);
#ifdef CONFIG_XENO_OPT_PERVASIVE
	if (wd->plink.last != wd->plink.next)
		/* Deleted watchdog was pending for delivery to the
		 * user-space server task: remove it from the
		 * list of events to process. */
		removeq(&wd->rh->wdpending, &wd->plink);
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	wind_mark_deleted(wd);
}
