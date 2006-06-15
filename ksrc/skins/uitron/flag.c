/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#include "uitron/task.h"
#include "uitron/flag.h"

static xnqueue_t uiflagq;

static uiflag_t *uiflagmap[uITRON_MAX_FLAGID];

void uiflag_init(void)
{
	initq(&uiflagq);
}

void uiflag_cleanup(void)
{
	xnholder_t *holder;

	while ((holder = getheadq(&uiflagq)) != NULL)
		del_flg(link2uiflag(holder)->flgid);
}

ER cre_flg(ID flgid, T_CFLG * pk_cflg)
{
	uiflag_t *flg;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	if (uiflagmap[flgid - 1] != NULL) {
		xnlock_put_irqrestore(&nklock, s);
		return E_OBJ;
	}

	uiflagmap[flgid - 1] = (uiflag_t *) 1;	/* Reserve slot */

	xnlock_put_irqrestore(&nklock, s);

	flg = (uiflag_t *) xnmalloc(sizeof(*flg));

	if (!flg) {
		uiflagmap[flgid - 1] = NULL;
		return E_NOMEM;
	}

	xnsynch_init(&flg->synchbase, XNSYNCH_FIFO);

	inith(&flg->link);
	flg->flgid = flgid;
	flg->exinf = pk_cflg->exinf;
	flg->flgatr = pk_cflg->flgatr;
	flg->flgvalue = pk_cflg->iflgptn;
	flg->magic = uITRON_FLAG_MAGIC;

	xnlock_get_irqsave(&nklock, s);
	uiflagmap[flgid - 1] = flg;
	appendq(&uiflagq, &flg->link);
	xnlock_put_irqrestore(&nklock, s);

	return E_OK;
}

ER del_flg(ID flgid)
{
	uiflag_t *flg;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	flg = uiflagmap[flgid - 1];

	if (!flg) {
		xnlock_put_irqrestore(&nklock, s);
		return E_NOEXS;
	}

	uiflagmap[flgid - 1] = NULL;

	ui_mark_deleted(flg);

	if (xnsynch_destroy(&flg->synchbase) == XNSYNCH_RESCHED)
		xnpod_schedule();

	xnlock_put_irqrestore(&nklock, s);

	xnfree(flg);

	return E_OK;
}

ER set_flg(ID flgid, UINT setptn)
{
	xnpholder_t *holder, *nholder;
	uiflag_t *flg;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	if (setptn == 0)
		return E_OK;

	xnlock_get_irqsave(&nklock, s);

	flg = uiflagmap[flgid - 1];

	if (!flg) {
		xnlock_put_irqrestore(&nklock, s);
		return E_NOEXS;
	}

	flg->flgvalue |= setptn;

	if (xnsynch_nsleepers(&flg->synchbase) > 0) {
		for (holder = getheadpq(xnsynch_wait_queue(&flg->synchbase));
		     holder; holder = nholder) {
			uitask_t *sleeper =
			    thread2uitask(link2thread(holder, plink));
			UINT wfmode = sleeper->wargs.flag.wfmode;
			UINT waiptn = sleeper->wargs.flag.waiptn;

			if (((wfmode & TWF_ORW)
			     && (waiptn & flg->flgvalue) != 0)
			    || (!(wfmode & TWF_ORW)
				&& ((waiptn & flg->flgvalue) == waiptn))) {
				nholder =
				    xnsynch_wakeup_this_sleeper(&flg->synchbase,
								holder);
				sleeper->wargs.flag.waiptn = flg->flgvalue;

				if (wfmode & TWF_CLR)
					flg->flgvalue = 0;
			} else
				nholder =
				    nextpq(xnsynch_wait_queue(&flg->synchbase),
					   holder);
		}

		xnpod_schedule();
	}

	xnlock_put_irqrestore(&nklock, s);

	return E_OK;
}

ER clr_flg(ID flgid, UINT clrptn)
{
	uiflag_t *flg;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	flg = uiflagmap[flgid - 1];

	if (!flg) {
		xnlock_put_irqrestore(&nklock, s);
		return E_NOEXS;
	}

	flg->flgvalue &= clrptn;

	xnlock_put_irqrestore(&nklock, s);

	return E_OK;
}

static ER wai_flg_helper(UINT *p_flgptn,
			 ID flgid, UINT waiptn, UINT wfmode, TMO tmout)
{
	xnticks_t timeout;
	uitask_t *task;
	uiflag_t *flg;
	int err;
	spl_t s;

	if (xnpod_unblockable_p())
		return E_CTX;

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

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	flg = uiflagmap[flgid - 1];

	if (!flg) {
		xnlock_put_irqrestore(&nklock, s);
		return E_NOEXS;
	}

	err = E_OK;

	if (((wfmode & TWF_ORW) && (waiptn & flg->flgvalue) != 0) ||
	    (!(wfmode & TWF_ORW) && ((waiptn & flg->flgvalue) == waiptn))) {
		*p_flgptn = flg->flgvalue;

		if (wfmode & TWF_CLR)
			flg->flgvalue = 0;
	} else if (timeout == XN_NONBLOCK)
		err = E_TMOUT;
	else if (xnsynch_nsleepers(&flg->synchbase) > 0 &&
		 !(flg->flgatr & TA_WMUL))
		err = E_OBJ;
	else {
		task = ui_current_task();

		xnsynch_sleep_on(&flg->synchbase, timeout);

		if (xnthread_test_flags(&task->threadbase, XNRMID))
			err = E_DLT;	/* Flag deleted while pending. */
		else if (xnthread_test_flags(&task->threadbase, XNTIMEO))
			err = E_TMOUT;	/* Timeout. */
		else if (xnthread_test_flags(&task->threadbase, XNBREAK))
			err = E_RLWAI;	/* rel_wai() received while waiting. */
		else
			*p_flgptn = task->wargs.flag.waiptn;
	}

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

ER ref_flg(T_RFLG * pk_rflg, ID flgid)
{
	uitask_t *sleeper;
	uiflag_t *flg;
	spl_t s;

	if (xnpod_asynch_p())
		return EN_CTXID;

	if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
		return E_ID;

	xnlock_get_irqsave(&nklock, s);

	flg = uiflagmap[flgid - 1];

	if (!flg) {
		xnlock_put_irqrestore(&nklock, s);
		return E_NOEXS;
	}

	sleeper =
	    thread2uitask(link2thread
			  (getheadpq(xnsynch_wait_queue(&flg->synchbase)),
			   plink));
	pk_rflg->exinf = flg->exinf;
	pk_rflg->flgptn = flg->flgvalue;
	pk_rflg->wtsk = sleeper ? sleeper->tskid : FALSE;

	xnlock_put_irqrestore(&nklock, s);

	return E_OK;
}
