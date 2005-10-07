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

#include "psos+/event.h"
#include "psos+/task.h"
#include "psos+/sem.h"
#include "psos+/asr.h"
#include "psos+/queue.h"
#include "psos+/pt.h"
#include "psos+/rn.h"
#include "psos+/tm.h"

MODULE_DESCRIPTION("pSOS+(R) virtual machine");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

static u_long rn0_size_arg = 32 * 1024; /* Default size of region #0 */
module_param_named(rn0_size,rn0_size_arg,ulong,0444);
MODULE_PARM_DESC(rn0_size,"Size of pSOS+ region #0 (in bytes)");

static u_long tick_hz_arg = 1000000000 / XNPOD_DEFAULT_TICK; /* Default tick period */
module_param_named(tick_hz,tick_hz_arg,ulong,0444);
MODULE_PARM_DESC(tick_hz,"Clock tick frequency (Hz)");

static u_long time_slice_arg = 10; /* Default (round-robin) time slice */
module_param_named(time_slice,time_slice_arg,ulong,0444);
MODULE_PARM_DESC(time_slice,"Default time slice (in ticks)");

static xnpod_t pod;

static void psos_shutdown (int xtype)

{
    xnpod_lock_sched();
    xnpod_stop_timer();

    psostask_cleanup();
    psostm_cleanup();
    psosasr_cleanup();
    psospt_cleanup();
    psosqueue_cleanup();
    psossem_cleanup();
    psosrn_cleanup();

    xnpod_shutdown(xtype);
}

void k_fatal (u_long err_code, u_long flags)

{
    xnpod_fatal("pSOS/vm: fatal error, code 0x%x",err_code);
}

int __xeno_skin_init (void)

{
    u_long nstick = XNPOD_DEFAULT_TICK;
    int err;

    err = xnpod_init(&pod,1,255,0);

    if (err != 0)
	return err;

    if (module_param_value(tick_hz_arg) > 0)
	nstick = 1000000000 / module_param_value(tick_hz_arg);

    err = xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);

    if (err == 0)
	err = psosrn_init(module_param_value(rn0_size_arg));

    if (err != 0)
        {
        xnpod_shutdown(XNPOD_FATAL_EXIT);
        return err;
        }

    psossem_init();
    psosqueue_init();
    psospt_init();
    psosasr_init();
    psostm_init();
    psostask_init(module_param_value(time_slice_arg));

    xnprintf("starting pSOS+ services.\n");

    return err;
}

void __xeno_skin_exit (void)

{
    xnprintf("stopping pSOS+ services.\n");
    psos_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__xeno_skin_init);
module_exit(__xeno_skin_exit);

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
