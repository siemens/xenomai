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

#include <vrtx/event.h>
#include <vrtx/task.h>
#include <vrtx/sem.h>
#include <vrtx/mb.h>
#include <vrtx/mx.h>
#include <vrtx/queue.h>
#include <vrtx/pt.h>
#include <vrtx/heap.h>
#include <vrtx/syscall.h>

MODULE_DESCRIPTION("VRTX(R) virtual machine");
MODULE_AUTHOR("jpinon@idealx.com, rpm@xenomai.org");
MODULE_LICENSE("GPL");

static u_long workspace_size_arg = 32 * 1024;	/* Default size of VRTX workspace */
module_param_named(workspace_size, workspace_size_arg, ulong, 0444);
MODULE_PARM_DESC(workspace_size, "Size of VRTX workspace (in bytes)");

static u_long task_stacksize_arg = 4096;	/* Default size of VRTX tasks */
module_param_named(task_stacksize, task_stacksize_arg, ulong, 0444);
MODULE_PARM_DESC(task_stacksize, "Default size of VRTX task stack (in bytes)");

#if !defined(__KERNEL__) || !defined(CONFIG_XENO_OPT_PERVASIVE)
static xnpod_t __vrtx_pod;
#endif /* !__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */

#ifdef CONFIG_XENO_EXPORT_REGISTRY
xnptree_t __vrtx_ptree = {

	.dir = NULL,
	.name = "vrtx",
	.entries = 0,
};
#endif /* CONFIG_XENO_EXPORT_REGISTRY */

vrtxidmap_t *vrtx_alloc_idmap(int maxids, int reserve)
{
	vrtxidmap_t *map;
	int mapsize;

	if (maxids > VRTX_MAX_IDS)
		return NULL;

	mapsize = sizeof(*map) + (maxids - 1) * sizeof(map->objarray[0]);
	map = (vrtxidmap_t *) xnmalloc(mapsize);

	if (!map)
		return NULL;

	map->usedids = 0;
	map->maxids = maxids;
	map->himask = reserve ? (((maxids / BITS_PER_LONG) / 2) << 1) - 1 : 0;
	map->himap = ~0;
	memset(map->lomap, ~0, sizeof(map->lomap));
	memset(map->objarray, 0, sizeof(map->objarray[0]) * maxids);

	return map;
}

void vrtx_free_idmap(vrtxidmap_t * map)
{
	xnfree(map);
}

int vrtx_get_id(vrtxidmap_t * map, int id, void *objaddr)
{
	int hi, lo;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (id >= 0) {
		if (map->objarray[id] != NULL) {
			id = -1;
			goto unlock_and_exit;
		}
	} else if (map->usedids >= map->maxids)
		goto unlock_and_exit;
	else {
		/* The himask implements a namespace reservation of half of
		   the bitmap space which cannot be used to draw ids. */

		hi = ffnz(map->himap & ~map->himask);

		if (!hi)
			goto unlock_and_exit;

		lo = ffnz(map->lomap[hi]);
		id = hi * BITS_PER_LONG + lo;
		++map->usedids;

		__clrbits(map->lomap[hi], 1 << lo);

		if (map->lomap[hi] == 0)
			__clrbits(map->himap, 1 << hi);
	}

	map->objarray[id] = objaddr;

      unlock_and_exit:

	xnlock_put_irqrestore(&nklock, s);

	return id;
}

void vrtx_put_id(vrtxidmap_t * map, int id)
{
	int hi = id / BITS_PER_LONG;
	int lo = id % BITS_PER_LONG;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	map->objarray[id] = NULL;
	__setbits(map->himap, 1 << hi);
	__setbits(map->lomap[hi], 1 << lo);
	--map->usedids;
	xnlock_put_irqrestore(&nklock, s);
}

int sc_gversion(void)
{
	return VRTX_SKIN_VERSION;
}

int SKIN_INIT(vrtx)
{
	int err;

#if CONFIG_XENO_OPT_TIMING_PERIOD == 0
	nktickdef = 1000000;	/* Defaults to 1ms. */
#endif

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	/* The VRTX skin is stacked over the core pod. */
	err = xncore_attach();
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
	/* The VRTX skin is standalone. */
	err = xnpod_init(&__vrtx_pod, 255, 0, XNREUSE);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

	if (err != 0)
		goto fail;

	if (!testbits(nkpod->status, XNTMPER)) {
		xnlogerr
		    ("incompatible timer mode (aperiodic found, need periodic).\n");
		err = -EBUSY;	/* Cannot work in aperiodic timing mode. */
	}

	if (err != 0) {
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
		xncore_detach(err);
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
		xnpod_shutdown(err);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
	      fail:
		xnlogerr("VRTX skin init failed, code %d.\n", err);
		return err;
	}

	/* the VRTX workspace, or sysheap, is accessed (sc_halloc) with
	 * hid #0.  We must ensure it is the first heap created, so
	 * vrtxheap_init must be called right now.
	 */
	err = vrtxheap_init(module_param_value(workspace_size_arg));

	if (err != 0)
		goto fail;

	vrtxevent_init();
	vrtxsem_init();
	vrtxqueue_init();
	vrtxpt_init();
	vrtxmb_init();
	vrtxmx_init();
	vrtxtask_init(module_param_value(task_stacksize_arg));
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	vrtxsys_init();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

	xnprintf("starting VRTX services.\n");

	return 0;
}

void SKIN_EXIT(vrtx)
{
	xnprintf("stopping VRTX services.\n");

	vrtxtask_cleanup();
	vrtxpt_cleanup();
	vrtxqueue_cleanup();
	vrtxmb_cleanup();
	vrtxmx_cleanup();
	vrtxsem_cleanup();
	vrtxevent_cleanup();
	vrtxheap_cleanup();
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	vrtxsys_cleanup();
	xncore_detach(XNPOD_NORMAL_EXIT);
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
	xnpod_shutdown(XNPOD_NORMAL_EXIT);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
}

module_init(__vrtx_skin_init);
module_exit(__vrtx_skin_exit);

EXPORT_SYMBOL(sc_accept);
EXPORT_SYMBOL(sc_adelay);
EXPORT_SYMBOL(sc_delay);
EXPORT_SYMBOL(sc_fclear);
EXPORT_SYMBOL(sc_fcreate);
EXPORT_SYMBOL(sc_fdelete);
EXPORT_SYMBOL(sc_finquiry);
EXPORT_SYMBOL(sc_fpend);
EXPORT_SYMBOL(sc_fpost);
EXPORT_SYMBOL(sc_gblock);
EXPORT_SYMBOL(sc_gclock);
EXPORT_SYMBOL(sc_gtime);
EXPORT_SYMBOL(sc_gversion);
EXPORT_SYMBOL(sc_halloc);
EXPORT_SYMBOL(sc_hcreate);
EXPORT_SYMBOL(sc_hdelete);
EXPORT_SYMBOL(sc_hfree);
EXPORT_SYMBOL(sc_hinquiry);
EXPORT_SYMBOL(sc_lock);
EXPORT_SYMBOL(sc_maccept);
EXPORT_SYMBOL(sc_mcreate);
EXPORT_SYMBOL(sc_mdelete);
EXPORT_SYMBOL(sc_minquiry);
EXPORT_SYMBOL(sc_mpend);
EXPORT_SYMBOL(sc_mpost);
EXPORT_SYMBOL(sc_pcreate);
EXPORT_SYMBOL(sc_pdelete);
EXPORT_SYMBOL(sc_pend);
EXPORT_SYMBOL(sc_pextend);
EXPORT_SYMBOL(sc_pinquiry);
EXPORT_SYMBOL(sc_post);
EXPORT_SYMBOL(sc_qaccept);
EXPORT_SYMBOL(sc_qbrdcst);
EXPORT_SYMBOL(sc_qcreate);
EXPORT_SYMBOL(sc_qdelete);
EXPORT_SYMBOL(sc_qecreate);
EXPORT_SYMBOL(sc_qinquiry);
EXPORT_SYMBOL(sc_qjam);
EXPORT_SYMBOL(sc_qpend);
EXPORT_SYMBOL(sc_qpost);
EXPORT_SYMBOL(sc_rblock);
EXPORT_SYMBOL(sc_saccept);
EXPORT_SYMBOL(sc_sclock);
EXPORT_SYMBOL(sc_screate);
EXPORT_SYMBOL(sc_sdelete);
EXPORT_SYMBOL(sc_sinquiry);
EXPORT_SYMBOL(sc_spend);
EXPORT_SYMBOL(sc_spost);
EXPORT_SYMBOL(sc_stime);
EXPORT_SYMBOL(sc_tcreate);
EXPORT_SYMBOL(sc_tdelete);
EXPORT_SYMBOL(sc_tecreate);
EXPORT_SYMBOL(sc_tinquiry);
EXPORT_SYMBOL(sc_tpriority);
EXPORT_SYMBOL(sc_tresume);
EXPORT_SYMBOL(sc_tslice);
EXPORT_SYMBOL(sc_tsuspend);
EXPORT_SYMBOL(sc_unlock);
EXPORT_SYMBOL(ui_timer);
