/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003,2006 Philippe Gerum <rpm@xenomai.org>.
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

#include <vrtx/task.h>
#include <vrtx/mx.h>

static xnmap_t *vrtx_mx_idmap;

static xnqueue_t vrtx_mx_q;

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	char owner[XNOBJECT_NAME_LEN];
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vrtxmx *mx = xnvfile_priv(it->vfile);
	struct xnthread *owner;

	priv->curr = getheadpq(xnsynch_wait_queue(&mx->synchbase));

	owner = xnsynch_owner(&mx->synchbase);
	if (owner)
		strncpy(priv->owner, xnthread_name(owner),
			sizeof(priv->owner));
	else
		*priv->owner = 0;

	return xnsynch_nsleepers(&mx->synchbase);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vrtxmx *mx = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&mx->synchbase),
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
		if (*priv->owner) {
			xnvfile_printf(it, "locked by %s\n", priv->owner);
			if (it->nrdata > 0)
				/* Mutex is pended -- dump waiters */
				xnvfile_printf(it, "-------------------------------------------\n");
		} else
			xnvfile_printf(it, "unlocked\n");
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

extern struct xnptree __vrtx_ptree;

static struct xnpnode_snapshot __mutex_pnode = {
	.node = {
		.dirname = "mutexes",
		.root = &__vrtx_ptree,
		.ops = &xnregistry_vfsnap_ops,
	},
	.vfile = {
		.privsz = sizeof(struct vfile_priv),
		.datasz = sizeof(struct vfile_data),
		.ops = &vfile_ops,
	},
};

#else /* !CONFIG_XENO_OPT_VFILE */

static struct xnpnode_snapshot __mutex_pnode = {
	.node = {
		.dirname = "mutexes",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

int mx_destroy_internal(vrtxmx_t *mx)
{
	int s = xnsynch_destroy(&mx->synchbase);
	xnmap_remove(vrtx_mx_idmap, mx->mid);
	removeq(&vrtx_mx_q, &mx->link);
	xnregistry_remove(mx->handle);
	xnfree(mx);
	return s;
}

int vrtxmx_init(void)
{
	initq(&vrtx_mx_q);
	vrtx_mx_idmap = xnmap_create(VRTX_MAX_MUTEXES, 0, 0);
	return vrtx_mx_idmap ? 0 : -ENOMEM;
}

void vrtxmx_cleanup(void)
{
	xnholder_t *holder;

	while ((holder = getheadq(&vrtx_mx_q)) != NULL)
		mx_destroy_internal(link2vrtxmx(holder));

	xnmap_delete(vrtx_mx_idmap);
}

int sc_mcreate(unsigned int opt, int *errp)
{
	int bflags, mid;
	vrtxmx_t *mx;
	spl_t s;

	switch (opt) {
	case 0:
		bflags = XNSYNCH_PRIO;
		break;
	case 1:
		bflags = XNSYNCH_FIFO;
		break;
	case 2:
		bflags = XNSYNCH_PRIO | XNSYNCH_PIP;
		break;
	default:
		*errp = ER_IIP;
		return 0;
	}

	mx = xnmalloc(sizeof(*mx));
	if (mx == NULL) {
		*errp = ER_NOCB;
		return -1;
	}

	mid = xnmap_enter(vrtx_mx_idmap, -1, mx);
	if (mid < 0) {
		xnfree(mx);
		return -1;
	}

	inith(&mx->link);
	mx->mid = mid;
	xnsynch_init(&mx->synchbase, bflags | XNSYNCH_DREORD | XNSYNCH_OWNER,
		     NULL);

	xnlock_get_irqsave(&nklock, s);
	appendq(&vrtx_mx_q, &mx->link);
	xnlock_put_irqrestore(&nklock, s);

	sprintf(mx->name, "mx%d", mid);
	xnregistry_enter(mx->name, mx, &mx->handle, &__mutex_pnode.node);

	*errp = RET_OK;

	return mid;
}

void sc_mpost(int mid, int *errp)
{
	xnthread_t *cur = xnpod_current_thread();
	vrtxmx_t *mx;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	mx = xnmap_fetch(vrtx_mx_idmap, mid);
	/* Return ER_ID if the poster does not own the mutex. */
	if (mx == NULL || xnsynch_owner(&mx->synchbase) != cur) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	*errp = RET_OK;

	if (xnsynch_release(&mx->synchbase))
		xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

void sc_mdelete(int mid, int opt, int *errp)
{
	xnthread_t *owner;
	vrtxmx_t *mx;
	spl_t s;

	if (opt & ~1) {
		*errp = ER_IIP;
		return;
	}

	xnlock_get_irqsave(&nklock, s);

	mx = xnmap_fetch(vrtx_mx_idmap, mid);
	if (mx == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	owner = xnsynch_owner(&mx->synchbase);
	if (owner && (opt == 0 || xnpod_current_thread() != owner)) {
		*errp = ER_PND;
		goto unlock_and_exit;
	}

	*errp = RET_OK;

	if (mx_destroy_internal(mx) == XNSYNCH_RESCHED)
		xnpod_schedule();

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

void sc_mpend(int mid, unsigned long timeout, int *errp)
{
	xnthread_t *cur = xnpod_current_thread();
	vrtxtask_t *task;
	vrtxmx_t *mx;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (xnpod_unblockable_p()) {
		*errp = -EPERM;
		goto unlock_and_exit;
	}

	mx = xnmap_fetch(vrtx_mx_idmap, mid);
	if (mx == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	*errp = RET_OK;

	if (xnsynch_owner(&mx->synchbase) == NULL) {
		xnsynch_set_owner(&mx->synchbase, cur);
		goto unlock_and_exit;
	}

	if (xnsynch_owner(&mx->synchbase) == cur)
		goto unlock_and_exit;

	task = thread2vrtxtask(cur);
	task->vrtxtcb.TCBSTAT = TBSMUTEX;

	if (timeout)
		task->vrtxtcb.TCBSTAT |= TBSDELAY;

	xnsynch_acquire(&mx->synchbase, timeout, XN_RELATIVE);

	if (xnthread_test_info(cur, XNBREAK))
		*errp = -EINTR;
	else if (xnthread_test_info(cur, XNRMID))
		*errp = ER_DEL;	/* Mutex deleted while pending. */
	else if (xnthread_test_info(cur, XNTIMEO))
		*errp = ER_TMO;	/* Timeout. */

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

void sc_maccept(int mid, int *errp)
{
	vrtxmx_t *mx;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (xnpod_unblockable_p()) {
		*errp = -EPERM;
		goto unlock_and_exit;
	}

	mx = xnmap_fetch(vrtx_mx_idmap, mid);
	if (mx == NULL) {
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	if (xnsynch_owner(&mx->synchbase) == NULL) {
		xnsynch_set_owner(&mx->synchbase, xnpod_current_thread());
		*errp = RET_OK;
	} else
		*errp = ER_PND;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}

int sc_minquiry(int mid, int *errp)
{
	vrtxmx_t *mx;
	spl_t s;
	int rc;

	xnlock_get_irqsave(&nklock, s);

	mx = xnmap_fetch(vrtx_mx_idmap, mid);
	if (mx == NULL) {
		rc = 0;
		*errp = ER_ID;
		goto unlock_and_exit;
	}

	rc = xnsynch_owner(&mx->synchbase) == NULL;

unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return rc;
}
