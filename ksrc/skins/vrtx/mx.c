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

static vrtxidmap_t *vrtx_mx_idmap;

static xnqueue_t vrtx_mx_q;

int mx_destroy_internal(vrtxmx_t *mx)
{
    int s = xnsynch_destroy(&mx->synchbase);
    vrtx_put_id(vrtx_mx_idmap, mx->mid);
    removeq(&vrtx_mx_q, &mx->link);
    xnfree(mx);
    return s;
}

int vrtxmx_init(void)
{
    initq(&vrtx_mx_q);
    vrtx_mx_idmap = vrtx_alloc_idmap(VRTX_MAX_MUTEXES, 0);
    return vrtx_mx_idmap ? 0 : -ENOMEM;
}

void vrtxmx_cleanup(void)
{
    xnholder_t *holder;

    while ((holder = getheadq(&vrtx_mx_q)) != NULL)
        mx_destroy_internal(link2vrtxmx(holder));

    vrtx_free_idmap(vrtx_mx_idmap);
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

    mx = (vrtxmx_t *)xnmalloc(sizeof(*mx));

    if (!mx) {
        *errp = ER_NOCB;
        return -1;
    }

    mid = vrtx_get_id(vrtx_mx_idmap, -1, mx);

    if (mid < 0) {
        xnfree(mx);
        return -1;
    }

    inith(&mx->link);
    mx->mid = mid;
    mx->owner = NULL;
    xnsynch_init(&mx->synchbase, bflags | XNSYNCH_DREORD);

    xnlock_get_irqsave(&nklock, s);
    appendq(&vrtx_mx_q, &mx->link);
    xnlock_put_irqrestore(&nklock, s);

    *errp = RET_OK;

    return mid;
}

void sc_mpost(int mid, int *errp)
{
    vrtxmx_t *mx;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    mx = (vrtxmx_t *)vrtx_get_object(vrtx_mx_idmap, mid);

    if (mx == NULL || mx->owner == NULL) {
        *errp = ER_ID;
        goto unlock_and_exit;
    }

    /* Undefined behaviour if the poster does not own the mutex. */
    mx->owner = xnsynch_wakeup_one_sleeper(&mx->synchbase);

    *errp = RET_OK;

    if (mx->owner)
        xnpod_schedule();

  unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);
}

void sc_mdelete(int mid, int opt, int *errp)
{
    vrtxmx_t *mx;
    spl_t s;

    if (opt & ~1) {
        *errp = ER_IIP;
        return;
    }

    xnlock_get_irqsave(&nklock, s);

    mx = (vrtxmx_t *)vrtx_get_object(vrtx_mx_idmap, mid);

    if (mx == NULL) {
        *errp = ER_ID;
        goto unlock_and_exit;
    }

    if (mx->owner && (opt == 0 || xnpod_current_thread() != mx->owner)) {
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
    vrtxtask_t *task;
    vrtxmx_t *mx;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (xnpod_unblockable_p()) {
        *errp = -EPERM;
        goto unlock_and_exit;
    }

    mx = (vrtxmx_t *)vrtx_get_object(vrtx_mx_idmap, mid);

    if (mx == NULL) {
        *errp = ER_ID;
        goto unlock_and_exit;
    }

    *errp = RET_OK;

    if (mx->owner != NULL) {
        task = vrtx_current_task();
        task->vrtxtcb.TCBSTAT = TBSMUTEX;

        if (timeout)
            task->vrtxtcb.TCBSTAT |= TBSDELAY;

        xnsynch_sleep_on(&mx->synchbase, timeout);

        if (xnthread_test_flags(&task->threadbase, XNBREAK))
            *errp = -EINTR;
        else if (xnthread_test_flags(&task->threadbase, XNRMID))
            *errp = ER_DEL;     /* Mutex deleted while pending. */
        else if (xnthread_test_flags(&task->threadbase, XNTIMEO))
            *errp = ER_TMO;     /* Timeout. */
        else
            goto grab_mutex;

        goto unlock_and_exit;
    }

  grab_mutex:

    mx->owner = xnpod_current_thread();

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

    mx = (vrtxmx_t *)vrtx_get_object(vrtx_mx_idmap, mid);

    if (mx == NULL) {
        *errp = ER_ID;
        goto unlock_and_exit;
    }

    if (mx->owner == NULL) {
        mx->owner = xnpod_current_thread();
        *errp = RET_OK;
    } else
        *errp = ER_PND;

  unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);
}

int sc_minquiry(int mid, int *errp)
{
    vrtxmx_t *mx;
    int rc = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    mx = (vrtxmx_t *)vrtx_get_object(vrtx_mx_idmap, mid);

    if (mx == NULL) {
        *errp = ER_ID;
        goto unlock_and_exit;
    }

    rc = mx->owner == NULL;

  unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);

    return rc;
}
