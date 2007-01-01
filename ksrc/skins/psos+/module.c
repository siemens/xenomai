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
#include <psos+/event.h>
#include <psos+/task.h>
#include <psos+/sem.h>
#include <psos+/asr.h>
#include <psos+/queue.h>
#include <psos+/pt.h>
#include <psos+/rn.h>
#include <psos+/tm.h>
#include <psos+/syscall.h>

MODULE_DESCRIPTION("pSOS+(R) virtual machine");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

static u_long tick_arg = CONFIG_XENO_OPT_PSOS_PERIOD;
module_param_named(tick_arg, tick_arg, ulong, 0444);
MODULE_PARM_DESC(tick_arg, "Fixed clock tick value (us)");

static u_long rn0_size_arg = 32 * 1024;	/* Default size of region #0 */
module_param_named(rn0_size, rn0_size_arg, ulong, 0444);
MODULE_PARM_DESC(rn0_size, "Size of pSOS+ region #0 (in bytes)");

static u_long time_slice_arg = 10;	/* Default (round-robin) time slice */
module_param_named(time_slice, time_slice_arg, ulong, 0444);
MODULE_PARM_DESC(time_slice, "Default time slice (in ticks)");

xntbase_t *psos_tbase;

#ifdef CONFIG_XENO_EXPORT_REGISTRY
xnptree_t __psos_ptree = {

	.dir = NULL,
	.name = "psos",
	.entries = 0,
};
#endif /* CONFIG_XENO_EXPORT_REGISTRY */

void k_fatal(u_long err_code, u_long flags)
{
	xnpod_fatal("pSOS skin: fatal error, code 0x%x", err_code);
}

int SKIN_INIT(psos)
{
	int err;

	err = xncore_attach(1, 255);

	if (err != 0)
		return err;

	err = xntbase_alloc("psos", tick_arg * 1000, &psos_tbase);

	if (err != 0)
		goto fail;

	xntbase_start(psos_tbase);

	err = psosrn_init(module_param_value(rn0_size_arg));

	if (err != 0) {
	fail:
		xncore_detach(err);
		xnlogerr("pSOS skin init failed, code %d.\n", err);
		return err;
	}

	psossem_init();
	psosqueue_init();
	psospt_init();
	psosasr_init();
	psostm_init();
	psostask_init(module_param_value(time_slice_arg));
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	psos_syscall_init();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

	xnprintf("starting pSOS+ services.\n");

	return err;
}

void SKIN_EXIT(psos)
{
	xnprintf("stopping pSOS+ services.\n");

	psostask_cleanup();
	psostm_cleanup();
	psosasr_cleanup();
	psospt_cleanup();
	psosqueue_cleanup();
	psossem_cleanup();
	psosrn_cleanup();
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	psos_syscall_cleanup();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
	xntbase_free(psos_tbase);
	xncore_detach(XNPOD_NORMAL_EXIT);
}

module_init(__psos_skin_init);
module_exit(__psos_skin_exit);

EXPORT_SYMBOL(as_catch);
EXPORT_SYMBOL(as_send);
EXPORT_SYMBOL(ev_receive);
EXPORT_SYMBOL(ev_send);
EXPORT_SYMBOL(k_fatal);
EXPORT_SYMBOL(pt_create);
EXPORT_SYMBOL(pt_delete);
EXPORT_SYMBOL(pt_getbuf);
EXPORT_SYMBOL(pt_ident);
EXPORT_SYMBOL(pt_retbuf);
EXPORT_SYMBOL(q_broadcast);
EXPORT_SYMBOL(q_create);
EXPORT_SYMBOL(q_delete);
EXPORT_SYMBOL(q_ident);
EXPORT_SYMBOL(q_receive);
EXPORT_SYMBOL(q_send);
EXPORT_SYMBOL(q_urgent);
EXPORT_SYMBOL(q_vbroadcast);
EXPORT_SYMBOL(q_vcreate);
EXPORT_SYMBOL(q_vdelete);
EXPORT_SYMBOL(q_vident);
EXPORT_SYMBOL(q_vreceive);
EXPORT_SYMBOL(q_vsend);
EXPORT_SYMBOL(q_vurgent);
EXPORT_SYMBOL(rn_create);
EXPORT_SYMBOL(rn_delete);
EXPORT_SYMBOL(rn_getseg);
EXPORT_SYMBOL(rn_ident);
EXPORT_SYMBOL(rn_retseg);
EXPORT_SYMBOL(sm_create);
EXPORT_SYMBOL(sm_delete);
EXPORT_SYMBOL(sm_ident);
EXPORT_SYMBOL(sm_p);
EXPORT_SYMBOL(sm_v);
EXPORT_SYMBOL(t_create);
EXPORT_SYMBOL(t_delete);
EXPORT_SYMBOL(t_getreg);
EXPORT_SYMBOL(t_ident);
EXPORT_SYMBOL(t_mode);
EXPORT_SYMBOL(t_restart);
EXPORT_SYMBOL(t_resume);
EXPORT_SYMBOL(t_setpri);
EXPORT_SYMBOL(t_setreg);
EXPORT_SYMBOL(t_start);
EXPORT_SYMBOL(t_suspend);
EXPORT_SYMBOL(tm_cancel);
EXPORT_SYMBOL(tm_evafter);
EXPORT_SYMBOL(tm_evevery);
EXPORT_SYMBOL(tm_evwhen);
EXPORT_SYMBOL(tm_get);
EXPORT_SYMBOL(tm_set);
EXPORT_SYMBOL(tm_tick);
EXPORT_SYMBOL(tm_wkafter);
EXPORT_SYMBOL(tm_wkwhen);
