/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <nucleus/registry.h>
#include <psos+/task.h>
#include <psos+/sem.h>

static xnqueue_t psossemq;

static int sm_destroy_internal(psossem_t *sem);

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	unsigned long value;
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	psossem_t *sem = xnvfile_priv(it->vfile);

	sem = psos_h2obj_active((u_long)sem, PSOS_SEM_MAGIC, psossem_t);
	if (sem == NULL)
		return -EIDRM;

	priv->curr = getheadpq(xnsynch_wait_queue(&sem->synchbase));
	priv->value = sem->count;

	return xnsynch_nsleepers(&sem->synchbase);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	psossem_t *sem = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	priv->value = sem->count; /* Refresh as we collect. */

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
		xnvfile_printf(it, "value=%lu\n", priv->value);
		if (it->nrdata > 0)
			/* Semaphore is pended -- dump waiters */
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

extern struct xnptree __psos_ptree;

static struct xnpnode_snapshot __sem_pnode = {
	.node = {
		.dirname = "semaphores",
		.root = &__psos_ptree,
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

void psossem_init(void)
{
	initq(&psossemq);
}

void psossem_cleanup(void)
{
	psos_sem_flush_rq(&__psos_global_rholder.smq);
}

u_long sm_create(const char *name, u_long icount, u_long flags, u_long *smid)
{
	static unsigned long sem_ids;
	int bflags = 0, ret;
	psossem_t *sem;
	spl_t s;

	sem = (psossem_t *)xnmalloc(sizeof(*sem));

	if (!sem)
		return ERR_NOSCB;

	if (flags & SM_PRIOR)
		bflags |= XNSYNCH_PRIO;

	xnsynch_init(&sem->synchbase, bflags, NULL);

	inith(&sem->link);
	sem->count = icount;
	sem->magic = PSOS_SEM_MAGIC;
	xnobject_copy_name(sem->name, name);

	inith(&sem->rlink);
	sem->rqueue = &psos_get_rholder()->smq;
	xnlock_get_irqsave(&nklock, s);
	appendq(sem->rqueue, &sem->rlink);
	appendq(&psossemq, &sem->link);
	xnlock_put_irqrestore(&nklock, s);

	if (!*name)
		sprintf(sem->name, "anon_sem%lu", sem_ids++);

	ret = xnregistry_enter(sem->name, sem, &sem->handle, &__sem_pnode.node);
	if (ret) {
		sem->handle = XN_NO_HANDLE;
		sm_delete((u_long)sem);
		return (u_long)ret;
	}

	*smid = (u_long)sem;

	return SUCCESS;
}

static int sm_destroy_internal(psossem_t *sem)
{
	int rc;

	removeq(sem->rqueue, &sem->rlink);
	removeq(&psossemq, &sem->link);
	rc = xnsynch_destroy(&sem->synchbase);
	if (sem->handle)
		xnregistry_remove(sem->handle);
	psos_mark_deleted(sem);

	xnfree(sem);

	return rc;
}

u_long sm_delete(u_long smid)
{
	u_long err = SUCCESS;
	psossem_t *sem;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = psos_h2obj_active(smid, PSOS_SEM_MAGIC, psossem_t);

	if (!sem) {
		err = psos_handle_error(smid, PSOS_SEM_MAGIC, psossem_t);
		goto unlock_and_exit;
	}

	if (sm_destroy_internal(sem) == XNSYNCH_RESCHED) {
		err = ERR_TATSDEL;
		xnpod_schedule();
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long sm_ident(const char *name, u_long node, u_long *smid)
{
	u_long err = SUCCESS;
	xnholder_t *holder;
	psossem_t *sem;
	spl_t s;

	if (node > 1)
		return ERR_NODENO;

	xnlock_get_irqsave(&nklock, s);

	for (holder = getheadq(&psossemq);
	     holder; holder = nextq(&psossemq, holder)) {
		sem = link2psossem(holder);

		if (!strcmp(sem->name, name)) {
			*smid = (u_long)sem;
			goto unlock_and_exit;
		}
	}

	err = ERR_OBJNF;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long sm_p(u_long smid, u_long flags, u_long timeout)
{
	u_long err = SUCCESS;
	psostask_t *task;
	psossem_t *sem;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = psos_h2obj_active(smid, PSOS_SEM_MAGIC, psossem_t);

	if (!sem) {
		err = psos_handle_error(smid, PSOS_SEM_MAGIC, psossem_t);
		goto unlock_and_exit;
	}

	if (flags & SM_NOWAIT) {
		if (sem->count > 0)
			sem->count--;
		else
			err = ERR_NOSEM;
	} else {
		if (xnpod_unblockable_p()) {
			err = -EPERM;
			goto unlock_and_exit;
		}

		if (sem->count > 0)
			sem->count--;
		else {
			xnsynch_sleep_on(&sem->synchbase, timeout, XN_RELATIVE);

			task = psos_current_task();

			if (xnthread_test_info(&task->threadbase, XNBREAK))
				err = -EINTR;
			else if (xnthread_test_info(&task->threadbase, XNRMID))
				err = ERR_SKILLD;	/* Semaphore deleted while pending. */
			else if (xnthread_test_info(&task->threadbase, XNTIMEO))
				err = ERR_TIMEOUT;	/* Timeout. */
		}
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

u_long sm_v(u_long smid)
{
	u_long err = SUCCESS;
	psossem_t *sem;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	sem = psos_h2obj_active(smid, PSOS_SEM_MAGIC, psossem_t);

	if (!sem) {
		err = psos_handle_error(smid, PSOS_SEM_MAGIC, psossem_t);
		goto unlock_and_exit;
	}

	if (xnsynch_wakeup_one_sleeper(&sem->synchbase) != NULL)
		xnpod_schedule();
	else
		sem->count++;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return err;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - Code executing on behalf of interrupt context is currently not
 * allowed to scan/alter the global sema4 queue (psossemq).
 */
