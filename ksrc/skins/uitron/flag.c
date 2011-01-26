/*
 * Copyright (C) 2001-2007 Philippe Gerum <rpm@xenomai.org>.
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

#include <nucleus/registry.h>
#include <nucleus/heap.h>
#include <uitron/task.h>
#include <uitron/flag.h>

static xnmap_t *ui_flag_idmap;

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	unsigned long value;
};

struct vfile_data {
	UINT wfmode;
	UINT waiptn;
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct uiflag *flag = xnvfile_priv(it->vfile);

	priv->curr = getheadpq(xnsynch_wait_queue(&flag->synchbase));
	priv->value = flag->flgvalue;

	return xnsynch_nsleepers(&flag->synchbase);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct uiflag *flag = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;
	struct uitask *task;

	priv->value = flag->flgvalue; /* Refresh as we collect. */

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&flag->synchbase),
			    priv->curr);

	/* Collect thread name to be output in ->show(). */
	strncpy(p->name, xnthread_name(thread), sizeof(p->name));
	task = thread2uitask(thread);
	p->wfmode = task->wargs.flag.wfmode;
	p->waiptn = task->wargs.flag.waiptn;

	return 1;
}

static int vfile_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vfile_data *p = data;

	if (p == NULL) {	/* Dump header. */
		/* Always dump current flag value. */
		xnvfile_printf(it, "=0x%lx\n", priv->value);
		if (it->nrdata > 0)
			xnvfile_printf(it, "\n%10s  %4s  %s\n",
				       "WAITPN", "WFMODE", "WAITER");
	} else
		xnvfile_printf(it, "0x%-8x  %4s  %.*s\n",
			       p->waiptn,
			       p->wfmode & TWF_ORW ? "OR" : "AND",
			       (int)sizeof(p->name), p->name);

	return 0;
}

static struct xnvfile_snapshot_ops vfile_ops = {
	.rewind = vfile_rewind,
	.next = vfile_next,
	.show = vfile_show,
};

extern struct xnptree __uitron_ptree;

static struct xnpnode_snapshot __flag_pnode = {
	.node = {
		.dirname = "flags",
		.root = &__uitron_ptree,
		.ops = &xnregistry_vfsnap_ops,
	},
	.vfile = {
		.privsz = sizeof(struct vfile_priv),
		.datasz = sizeof(struct vfile_data),
		.ops = &vfile_ops,
	},
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_snapshot __flag_pnode = {
	.node = {
		.dirname = "flags",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

int uiflag_init(void)
{
	ui_flag_idmap = xnmap_create(uITRON_MAX_FLAGID, uITRON_MAX_FLAGID, 1);
	return ui_flag_idmap ? 0 : -ENOMEM;
}

void uiflag_cleanup(void)
{
	ui_flag_flush_rq(&__ui_global_rholder.flgq);
	xnmap_delete(ui_flag_idmap);
}

ER cre_flg(ID flgid, T_CFLG *pk_cflg)
{
	uiflag_t *flag;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_MBXID)
		return E_ID;

	flag = xnmalloc(sizeof(*flag));

	if (!flag)
		return E_NOMEM;

	flgid = xnmap_enter(ui_flag_idmap, flgid, flag);

	if (flgid <= 0) {
		xnfree(flag);
		return E_OBJ;
	}

	xnsynch_init(&flag->synchbase, XNSYNCH_FIFO, NULL);
	flag->id = flgid;
	flag->exinf = pk_cflg->exinf;
	flag->flgatr = pk_cflg->flgatr;
	flag->flgvalue = pk_cflg->iflgptn;
	sprintf(flag->name, "flg%d", flgid);
	xnregistry_enter(flag->name, flag, &flag->handle, &__flag_pnode.node);
	xnarch_memory_barrier();
	flag->magic = uITRON_FLAG_MAGIC;

	return E_OK;
}

ER del_flg(ID flgid)
{
	uiflag_t *flag;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	flag = xnmap_fetch(ui_flag_idmap, flgid);

	if (!flag) {
		xnlock_put_irqrestore(&nklock, s);
		return E_NOEXS;
	}

	xnmap_remove(ui_flag_idmap, flag->id);
	ui_mark_deleted(flag);

	xnregistry_remove(flag->handle);
	xnfree(flag);

	if (xnsynch_destroy(&flag->synchbase) == XNSYNCH_RESCHED)
		xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	return E_OK;
}

ER set_flg(ID flgid, UINT setptn)
{
	xnpholder_t *holder, *nholder;
	uiflag_t *flag;
	ER err = E_OK;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	flag = xnmap_fetch(ui_flag_idmap, flgid);

	if (!flag) {
		err = E_NOEXS;
		goto unlock_and_exit;
	}

	if (setptn == 0)
		goto unlock_and_exit;

	flag->flgvalue |= setptn;

	if (!xnsynch_pended_p(&flag->synchbase))
		goto unlock_and_exit;

	nholder = getheadpq(xnsynch_wait_queue(&flag->synchbase));

	while ((holder = nholder) != NULL) {
		uitask_t *sleeper = thread2uitask(link2thread(holder, plink));
		UINT wfmode = sleeper->wargs.flag.wfmode;
		UINT waiptn = sleeper->wargs.flag.waiptn;

		if (((wfmode & TWF_ORW) && (waiptn & flag->flgvalue) != 0)
		    || (!(wfmode & TWF_ORW) && ((waiptn & flag->flgvalue) == waiptn))) {
			nholder = xnsynch_wakeup_this_sleeper(&flag->synchbase, holder);
			sleeper->wargs.flag.waiptn = flag->flgvalue;

			if (wfmode & TWF_CLR)
				flag->flgvalue = 0;
		} else
			nholder = nextpq(xnsynch_wait_queue(&flag->synchbase), holder);
	}

	xnpod_schedule();


unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

ER clr_flg(ID flgid, UINT clrptn)
{
	uiflag_t *flag;
	ER err = E_OK;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	flag = xnmap_fetch(ui_flag_idmap, flgid);

	if (!flag) {
		err = E_NOEXS;
		goto unlock_and_exit;
	}

	flag->flgvalue &= clrptn;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

static ER wai_flg_helper(UINT *p_flgptn,
			 ID flgid, UINT waiptn, UINT wfmode, TMO tmout)
{
	xnticks_t timeout;
	uitask_t *task;
	uiflag_t *flag;
	ER err = E_OK;
	spl_t s;

	if (xnpod_unblockable_p())
		return E_CTX;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	if (waiptn == 0)
		return E_PAR;

	if (tmout == TMO_FEVR)
		timeout = XN_INFINITE;
	else if (tmout == 0)
		timeout = XN_NONBLOCK;
	else if (tmout < TMO_FEVR)
		return E_PAR;
	else
		timeout = (xnticks_t)tmout;

	xnlock_get_irqsave(&nklock, s);

	flag = xnmap_fetch(ui_flag_idmap, flgid);

	if (!flag) {
		err = E_NOEXS;
		goto unlock_and_exit;
	}

	if (((wfmode & TWF_ORW) && (waiptn & flag->flgvalue) != 0) ||
	    (!(wfmode & TWF_ORW) && ((waiptn & flag->flgvalue) == waiptn))) {
		*p_flgptn = flag->flgvalue;

		if (wfmode & TWF_CLR)
			flag->flgvalue = 0;

		goto unlock_and_exit;
	}

	if (timeout == XN_NONBLOCK) {
		err = E_TMOUT;
		goto unlock_and_exit;
	}

	else if (xnsynch_pended_p(&flag->synchbase) && !(flag->flgatr & TA_WMUL)) {
		err = E_OBJ;
		goto unlock_and_exit;
	}

	task = ui_current_task();

	xnthread_clear_info(&task->threadbase, uITRON_TASK_RLWAIT);
	task->wargs.flag.wfmode = wfmode;
	task->wargs.flag.waiptn = waiptn;

	xnsynch_sleep_on(&flag->synchbase, timeout, XN_RELATIVE);

	if (xnthread_test_info(&task->threadbase, XNRMID))
		err = E_DLT;	/* Flag deleted while pending. */
	else if (xnthread_test_info(&task->threadbase, XNTIMEO))
		err = E_TMOUT;	/* Timeout. */
	else if (xnthread_test_info(&task->threadbase, XNBREAK))
		err = E_RLWAI;	/* rel_wai() or signal received while waiting. */
	else
		*p_flgptn = task->wargs.flag.waiptn;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

ER wai_flg(UINT *p_flgptn, ID flgid, UINT waiptn, UINT wfmode)
{
	return wai_flg_helper(p_flgptn, flgid, waiptn, wfmode, TMO_FEVR);
}

ER pol_flg(UINT *p_flgptn, ID flgid, UINT waiptn, UINT wfmode)
{
	return wai_flg_helper(p_flgptn, flgid, waiptn, wfmode, 0);
}

ER twai_flg(UINT *p_flgptn, ID flgid, UINT waiptn, UINT wfmode, TMO tmout)
{
	return wai_flg_helper(p_flgptn, flgid, waiptn, wfmode, tmout);
}

ER ref_flg(T_RFLG *pk_rflg, ID flgid)
{
	uitask_t *sleeper;
	uiflag_t *flag;
	ER err = E_OK;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	flag = xnmap_fetch(ui_flag_idmap, flgid);

	if (!flag) {
		err = E_NOEXS;
		goto unlock_and_exit;
	}

	if (xnsynch_pended_p(&flag->synchbase)) {
		xnpholder_t *holder = getheadpq(xnsynch_wait_queue(&flag->synchbase));
		xnthread_t *thread = link2thread(holder, plink);
		sleeper = thread2uitask(thread);
		pk_rflg->wtsk = sleeper->id;
	} else
		pk_rflg->wtsk = FALSE;

	pk_rflg->exinf = flag->exinf;
	pk_rflg->flgptn = flag->flgvalue;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

EXPORT_SYMBOL_GPL(cre_flg);
EXPORT_SYMBOL_GPL(del_flg);
EXPORT_SYMBOL_GPL(set_flg);
EXPORT_SYMBOL_GPL(clr_flg);
EXPORT_SYMBOL_GPL(wai_flg);
EXPORT_SYMBOL_GPL(pol_flg);
EXPORT_SYMBOL_GPL(twai_flg);
EXPORT_SYMBOL_GPL(ref_flg);
