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

#define MB_HASHBITS 8

static vrtxmb_t *jhash_buckets[1 << MB_HASHBITS];   /* Guaranteed zero */

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
        mb_unhash(mb->mboxp);
        xnfree(mb);
    }
}

char *sc_accept(char **mboxp, int *errp)
{
    char *msg;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    msg = *mboxp;

    if (msg == NULL)
        *errp = ER_NMP;
    else {
        *mboxp = NULL;
        *errp = RET_OK;
    }

    xnlock_put_irqrestore(&nklock, s);

    return msg;
}

/*
 * Manages a hash of xnsynch_t objects, indexed by mailboxes
 * addresses.  Given a mailbox, returns its synch.  If the synch is
 * not found, creates one. Must be called interrupts off, nklock
 * locked.
 */

xnsynch_t *mb_map(char **mboxp)
{
    vrtxmb_t *mb = mb_find(mboxp);

    if (mb)
        return &mb->synchbase;

    /* New mailbox, create a new slot for it. */

    mb = (vrtxmb_t *) xnmalloc(sizeof(*mb));

    if (!mb)
        return NULL;

    inith(&mb->link);
    mb->mboxp = mboxp;
    mb->hnext = NULL;
    xnsynch_init(&mb->synchbase, XNSYNCH_PRIO | XNSYNCH_DREORD);
    appendq(&vrtx_mbox_q, &mb->link);
    mb_hash(mboxp, mb);

    return &mb->synchbase;
}

char *sc_pend(char **mboxp, long timeout, int *errp)
{
    xnsynch_t *synchbase;
    vrtxtask_t *task;
    char *msg;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    msg = *mboxp;

    if (msg != NULL) {
        *mboxp = NULL;          /* Consume the message and exit. */
        goto done;
    }

    if (xnpod_unblockable_p()) {
        *errp = -EPERM;
        goto unlock_and_exit;
    }

    synchbase = mb_map(mboxp);

    if (!synchbase) {
        *errp = ER_NOCB;
        goto unlock_and_exit;
    }

    task = vrtx_current_task();
    task->vrtxtcb.TCBSTAT = TBSMBOX;

    if (timeout)
        task->vrtxtcb.TCBSTAT |= TBSDELAY;

    xnsynch_sleep_on(synchbase, timeout);

    if (xnthread_test_flags(&task->threadbase, XNBREAK)) {
        *errp = -EINTR;
        goto unlock_and_exit;
    }

    if (xnthread_test_flags(&task->threadbase, XNTIMEO)) {
        *errp = ER_TMO;
        goto unlock_and_exit;
    }

    msg = task->waitargs.msg;

  done:

    *errp = RET_OK;

  unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);

    return msg;
}

void sc_post(char **mboxp, char *msg, int *errp)
{
    xnsynch_t *synchbase;
    xnthread_t *waiter;
    spl_t s;

    if (msg == NULL) {
        *errp = ER_ZMW;
        return;
    }

    xnlock_get_irqsave(&nklock, s);

    if (*mboxp != NULL) {
        *errp = ER_MIU;
        goto unlock_and_exit;
    }

    *errp = RET_OK;

    synchbase = mb_map(mboxp);

    if (!synchbase) {
        *errp = ER_NOCB;
        goto unlock_and_exit;
    }

    /* xnsynch_wakeup_one_sleeper() readies the thread */
    waiter = xnsynch_wakeup_one_sleeper(synchbase);

    if (waiter) {
        thread2vrtxtask(waiter)->waitargs.msg = msg;
        xnpod_schedule();
    } else
        *mboxp = msg;

  unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);
}
