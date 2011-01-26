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
#include <uitron/sem.h>

static xnmap_t *ui_sem_idmap;

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	int semcnt;
	int sematr;
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct uisem *sem = xnvfile_priv(it->vfile);

	priv->curr = getheadpq(xnsynch_wait_queue(&sem->synchbase));
	priv->semcnt = sem->semcnt;
	priv->sematr = sem->sematr;

	return xnsynch_nsleepers(&sem->synchbase);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct uisem *sem = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	priv->semcnt = sem->semcnt; /* Refresh as we collect. */

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&sem->synchbase),
			    priv->curr);

	/* Collect thread name to be output in ->show(). */
	strncpy(p->name, xnthread_name(thread), sizeof(p->name));

	return 1;
}

static int vfile_show(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vfile_data *p = data;

	if (p == NULL) {	/* Dump header. */
		/* Always dump current semaphore value. */
		xnvfile_printf(it, "count=%d, attr=%s\n",
			       priv->semcnt,
			       priv->sematr & TA_TPRI ? "TA_TPRI" : "TA_TFIFO");
		if (it->nrdata > 0)
			xnvfile_printf(it, "--------------------\n");
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

extern struct xnptree __uitron_ptree;

static struct xnpnode_snapshot __sem_pnode = {
	.node = {
		.dirname = "semaphores",
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

static struct xnpnode_snapshot __sem_pnode = {
	.node = {
		.dirname = "semaphores",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

int uisem_init(void)
{
	ui_sem_idmap = xnmap_create(uITRON_MAX_SEMID, uITRON_MAX_SEMID, 1);
	return ui_sem_idmap ? 0 : -ENOMEM;
}

void uisem_cleanup(void)
{
	ui_sem_flush_rq(&__ui_global_rholder.semq);
	xnmap_delete(ui_sem_idmap);
}

ER cre_sem(ID semid, T_CSEM *pk_csem)
{
	uisem_t *sem;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (pk_csem->isemcnt < 0 ||
	    pk_csem->maxsem < 0 || pk_csem->isemcnt > pk_csem->maxsem)
		return E_PAR;

	if (semid <= 0 || semid > uITRON_MAX_SEMID)
		return E_ID;

	sem = xnmalloc(sizeof(*sem));

	if (!sem)
		return E_NOMEM;

	semid = xnmap_enter(ui_sem_idmap, semid, sem);

	if (semid <= 0) {
		xnfree(sem);
		return E_OBJ;
	}

	xnsynch_init(&sem->synchbase,
		     (pk_csem->sematr & TA_TPRI) ? XNSYNCH_PRIO : XNSYNCH_FIFO,
		     NULL);

	sem->id = semid;
	sem->exinf = pk_csem->exinf;
	sem->sematr = pk_csem->sematr;
	sem->semcnt = pk_csem->isemcnt;
	sem->maxsem = pk_csem->maxsem;
	sprintf(sem->name, "sem%d", semid);
	xnregistry_enter(sem->name, sem, &sem->handle, &__sem_pnode.node);
	xnarch_memory_barrier();
	sem->magic = uITRON_SEM_MAGIC;

	return E_OK;
}

ER del_sem(ID semid)
{
	uisem_t *sem;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (semid <= 0 || semid > uITRON_MAX_SEMID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	sem = xnmap_fetch(ui_sem_idmap, semid);

	if (!sem) {
		xnlock_put_irqrestore(&nklock, s);
		return E_NOEXS;
	}

	xnmap_remove(ui_sem_idmap, sem->id);
	ui_mark_deleted(sem);

	xnregistry_remove(sem->handle);
	xnfree(sem);

	if (xnsynch_destroy(&sem->synchbase) == XNSYNCH_RESCHED)
		xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	return E_OK;
}

ER sig_sem(ID semid)
{
	uitask_t *sleeper;
	ER err = E_OK;
	uisem_t *sem;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (semid <= 0 || semid > uITRON_MAX_SEMID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	sem = xnmap_fetch(ui_sem_idmap, semid);

	if (!sem) {
		err = E_NOEXS;
		goto unlock_and_exit;
	}

	if (xnsynch_pended_p(&sem->synchbase)) {
		sleeper = thread2uitask(xnsynch_wakeup_one_sleeper(&sem->synchbase));
		xnpod_schedule();
		goto unlock_and_exit;
	}

	if (++sem->semcnt > sem->maxsem || sem->semcnt < 0) {
		sem->semcnt--;
		err = E_QOVR;
	}

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

static ER wai_sem_helper(ID semid, TMO tmout)
{
	xnticks_t timeout;
	uitask_t *task;
	ER err = E_OK;
	uisem_t *sem;
	spl_t s;

	if (xnpod_unblockable_p())
		return E_CTX;

	if (tmout == TMO_FEVR)
		timeout = XN_INFINITE;
	else if (tmout == 0)
		timeout = XN_NONBLOCK;
	else if (tmout < TMO_FEVR)
		return E_PAR;
	else
		timeout = (xnticks_t)tmout;

	if (semid <= 0 || semid > uITRON_MAX_SEMID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	sem = xnmap_fetch(ui_sem_idmap, semid);

	if (!sem) {
		err = E_NOEXS;
		goto unlock_and_exit;
	}

	if (sem->semcnt > 0) {
		sem->semcnt--;
		goto unlock_and_exit;
	}

	else if (timeout == XN_NONBLOCK) {
		err = E_TMOUT;
		goto unlock_and_exit;
	}

	task = ui_current_task();

	xnthread_clear_info(&task->threadbase, uITRON_TASK_RLWAIT);

	xnsynch_sleep_on(&sem->synchbase, timeout, XN_RELATIVE);

	if (xnthread_test_info(&task->threadbase, XNRMID))
		err = E_DLT;	/* Semaphore deleted while pending. */
	else if (xnthread_test_info(&task->threadbase, XNTIMEO))
		err = E_TMOUT;	/* Timeout. */
	else if (xnthread_test_info(&task->threadbase, XNBREAK))
		err = E_RLWAI;	/* rel_wai() or signal received while waiting. */

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

ER wai_sem(ID semid)
{
	return wai_sem_helper(semid, TMO_FEVR);
}

ER preq_sem(ID semid)
{
	return wai_sem_helper(semid, 0);
}

ER twai_sem(ID semid, TMO tmout)
{
	return wai_sem_helper(semid, tmout);
}

ER ref_sem(T_RSEM *pk_rsem, ID semid)
{
	uitask_t *sleeper;
	uisem_t *sem;
	spl_t s;

	if (semid <= 0 || semid > uITRON_MAX_SEMID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	sem = xnmap_fetch(ui_sem_idmap, semid);

	if (!sem) {
		xnlock_put_irqrestore(&nklock, s);
		return E_NOEXS;
	}

	if (xnsynch_pended_p(&sem->synchbase)) {
		sleeper =
			thread2uitask(link2thread
				      (getheadpq(xnsynch_wait_queue(&sem->synchbase)),
				       plink));
		pk_rsem->wtsk = sleeper->id;
	} else
		pk_rsem->wtsk = FALSE;

	pk_rsem->exinf = sem->exinf;
	pk_rsem->semcnt = sem->semcnt;

	xnlock_put_irqrestore(&nklock, s);

	return E_OK;
}

EXPORT_SYMBOL_GPL(cre_sem);
EXPORT_SYMBOL_GPL(del_sem);
EXPORT_SYMBOL_GPL(sig_sem);
EXPORT_SYMBOL_GPL(wai_sem);
EXPORT_SYMBOL_GPL(preq_sem);
EXPORT_SYMBOL_GPL(twai_sem);
EXPORT_SYMBOL_GPL(ref_sem);
