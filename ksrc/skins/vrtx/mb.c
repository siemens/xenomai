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

#include <nucleus/jhash.h>
#include <vrtx/task.h>
#include <vrtx/mb.h>

static xnqueue_t vrtx_mbox_q;

/* Note: In the current implementation, mailbox addresses passed to
 * the VRTX services are never dereferenced, but only used as hash
 * keys. */

#ifdef CONFIG_XENO_OPT_VFILE

struct vfile_priv {
	struct xnpholder *curr;
	char *msg;
};

struct vfile_data {
	char name[XNOBJECT_NAME_LEN];
};

static int vfile_rewind(struct xnvfile_snapshot_iterator *it)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vrtxmb *mb = xnvfile_priv(it->vfile);

	priv->curr = getheadpq(xnsynch_wait_queue(&mb->synchbase));
	priv->msg = mb->msg;

	return xnsynch_nsleepers(&mb->synchbase);
}

static int vfile_next(struct xnvfile_snapshot_iterator *it, void *data)
{
	struct vfile_priv *priv = xnvfile_iterator_priv(it);
	struct vrtxmb *mb = xnvfile_priv(it->vfile);
	struct vfile_data *p = data;
	struct xnthread *thread;

	if (priv->curr == NULL)
		return 0;	/* We are done. */

	/* Fetch current waiter, advance list cursor. */
	thread = link2thread(priv->curr, plink);
	priv->curr = nextpq(xnsynch_wait_queue(&mb->synchbase),
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
		if (it->nrdata == 0)
			xnvfile_printf(it, "=%p\n", priv->msg);
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

static struct xnpnode_snapshot __mb_pnode = {
	.node = {
		.dirname = "mailboxes",
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

static struct xnpnode_snapshot __mb_pnode = {
	.node = {
		.dirname = "mailboxes",
	},
};

#endif /* !CONFIG_XENO_OPT_VFILE */

#define MB_HASHBITS 8

static vrtxmb_t *jhash_buckets[1 << MB_HASHBITS];	/* Guaranteed zero */

union jhash_union {

	char **key;
	uint32_t val;
};

static void mb_hash(char **pkey, vrtxmb_t * mb)
{
	union jhash_union hkey = {.key = pkey };
	vrtxmb_t **bucketp;
	uint32_t hash;
	spl_t s;

	hash = jhash2(&hkey.val, sizeof(pkey) / sizeof(uint32_t), 0);
	bucketp = &jhash_buckets[hash & ((1 << MB_HASHBITS) - 1)];

	xnlock_get_irqsave(&nklock, s);
	mb->hnext = *bucketp;
	*bucketp = mb;
	xnlock_put_irqrestore(&nklock, s);
}

static void mb_unhash(char **pkey)
{
	union jhash_union hkey = {.key = pkey };
	vrtxmb_t **tail, *mb;
	uint32_t hash;
	spl_t s;

	hash = jhash2(&hkey.val, sizeof(pkey) / sizeof(uint32_t), 0);
	tail = &jhash_buckets[hash & ((1 << MB_HASHBITS) - 1)];

	xnlock_get_irqsave(&nklock, s);

	mb = *tail;

	while (mb != NULL && mb->mboxp != pkey) {
		tail = &mb->hnext;
		mb = *tail;
	}

	if (mb)
		*tail = mb->hnext;

	xnlock_put_irqrestore(&nklock, s);
}

static vrtxmb_t *mb_find(char **pkey)
{
	union jhash_union hkey = {.key = pkey };
	uint32_t hash;
	vrtxmb_t *mb;
	spl_t s;

	hash = jhash2(&hkey.val, sizeof(pkey) / sizeof(uint32_t), 0);

	xnlock_get_irqsave(&nklock, s);

	mb = jhash_buckets[hash & ((1 << MB_HASHBITS) - 1)];

	while (mb != NULL && mb->mboxp != pkey)
		mb = mb->hnext;

	xnlock_put_irqrestore(&nklock, s);

	return mb;
}

void vrtxmb_init(void)
{
	initq(&vrtx_mbox_q);
}

void vrtxmb_cleanup(void)
{
	xnholder_t *holder;
	vrtxmb_t *mb;

	while ((holder = getq(&vrtx_mbox_q)) != NULL) {
		mb = link2vrtxmb(holder);
		xnsynch_destroy(&mb->synchbase);
		xnregistry_remove(mb->handle);
		mb_unhash(mb->mboxp);
		xnfree(mb);
	}
}

/*
 * Manages a hash of xnsynch_t objects, indexed by mailboxes
 * addresses.  Given a mailbox, returns its descriptor address.  If
 * the mailbox is not found, creates a descriptor for it. Must be
 * called interrupts off, nklock locked.
 */

vrtxmb_t *mb_map(char **mboxp)
{
	vrtxmb_t *mb = mb_find(mboxp);

	if (mb)
		return mb;

	/* New mailbox, create a new slot for it. */

	mb = (vrtxmb_t *) xnmalloc(sizeof(*mb));

	if (!mb)
		return NULL;

	inith(&mb->link);
	mb->mboxp = mboxp;
	mb->msg = NULL;
	mb->hnext = NULL;
	xnsynch_init(&mb->synchbase, XNSYNCH_PRIO | XNSYNCH_DREORD, NULL);
	appendq(&vrtx_mbox_q, &mb->link);
	mb_hash(mboxp, mb);

	sprintf(mb->name, "mb@%p", mboxp);
	xnregistry_enter(mb->name, mb, &mb->handle, &__mb_pnode.node);

	return mb;
}

char *sc_accept(char **mboxp, int *errp)
{
	char *msg = NULL;
	vrtxmb_t *mb;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	mb = mb_map(mboxp);

	if (!mb) {
		*errp = ER_NOCB;
		goto unlock_and_exit;
	}

	msg = mb->msg;

	if (msg == NULL)
		*errp = ER_NMP;
	else {
		mb->msg = NULL;
		*errp = RET_OK;
	}

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return msg;
}

char *sc_pend(char **mboxp, long timeout, int *errp)
{
	char *msg = NULL;
	vrtxtask_t *task;
	vrtxmb_t *mb;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	mb = mb_map(mboxp);

	if (!mb) {
		*errp = ER_NOCB;
		goto unlock_and_exit;
	}

	if (mb->msg != NULL)
		goto done;

	if (xnpod_unblockable_p()) {
		*errp = -EPERM;
		goto unlock_and_exit;
	}

	task = vrtx_current_task();
	task->vrtxtcb.TCBSTAT = TBSMBOX;

	if (timeout)
		task->vrtxtcb.TCBSTAT |= TBSDELAY;

	xnsynch_sleep_on(&mb->synchbase, timeout, XN_RELATIVE);

	if (xnthread_test_info(&task->threadbase, XNBREAK)) {
		*errp = -EINTR;
		goto unlock_and_exit;
	}

	if (xnthread_test_info(&task->threadbase, XNTIMEO)) {
		*errp = ER_TMO;
		goto unlock_and_exit;
	}

      done:

	msg = mb->msg;
	mb->msg = NULL;
	*errp = RET_OK;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return msg;
}

void sc_post(char **mboxp, char *msg, int *errp)
{
	vrtxmb_t *mb;
	spl_t s;

	if (msg == NULL) {
		*errp = ER_ZMW;
		return;
	}

	xnlock_get_irqsave(&nklock, s);

	mb = mb_map(mboxp);

	if (!mb) {
		*errp = ER_NOCB;
		goto unlock_and_exit;
	}

	if (mb->msg != NULL) {
		*errp = ER_MIU;
		goto unlock_and_exit;
	}

	mb->msg = msg;
	*errp = RET_OK;

	/* xnsynch_wakeup_one_sleeper() readies the front thread */
	if (xnsynch_wakeup_one_sleeper(&mb->synchbase))
		xnpod_schedule();

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);
}
