/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef __KERNEL__
#include <xenomai/posix/syscall.h>
#endif /* __KERNEL__ */
#include <xenomai/posix/posix.h>
#include <posix/internal.h>
#include <posix/cond.h>
#include <posix/mutex.h>
#include <posix/sem.h>
#include <posix/sig.h>
#include <posix/thread.h>
#include <posix/tsd.h>
#include <posix/mq.h>
#include <posix/intr.h>
#include <posix/timer.h>
#include <posix/registry.h>
#include <posix/shm.h>

MODULE_DESCRIPTION("POSIX/PSE51 interface");
MODULE_AUTHOR("gilles.chanteperdrix@laposte.net");
MODULE_LICENSE("GPL");

static u_long time_slice_arg = 1; /* Default (round-robin) time slice */
module_param_named(time_slice,time_slice_arg,ulong,0444);
MODULE_PARM_DESC(time_slice,"Default time slice (in ticks)");

#if !defined(__KERNEL__) || !defined(CONFIG_XENO_OPT_PERVASIVE)
static xnpod_t pod;
#endif /* !defined(__KERNEL__) || !defined(CONFIG_XENO_OPT_PERVASIVE) */

static void pse51_shutdown(int xtype)
{
    xnpod_stop_timer();

    pse51_shm_pkg_cleanup();
    pse51_thread_pkg_cleanup();
    pse51_timer_pkg_cleanup();
    pse51_tsd_pkg_cleanup();
    pse51_cond_pkg_cleanup();
    pse51_sem_pkg_cleanup();
    pse51_mutex_pkg_cleanup();
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    pse51_intr_pkg_cleanup();
    pse51_syscall_cleanup();
    xncore_detach();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
    pse51_reg_pkg_cleanup();
    pse51_mq_pkg_cleanup();
    pse51_signal_pkg_cleanup();

    xnpod_shutdown(xtype);
}

int SKIN_INIT(posix)
{
    int err;

    xnprintf("starting POSIX services.\n");

    nktickdef = XN_APERIODIC_TICK;	/* Defaults to aperiodic. */

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    /* The POSIX skin is stacked over the shared Xenomai pod. */
    err = xncore_attach();
#else /* !(__KERNEL__ && CONFIG_XENO_OPT_PERVASIVE) */
    /* The POSIX skin is standalone. */
    err = xnpod_init(&pod,PSE51_MIN_PRIORITY,PSE51_MAX_PRIORITY,0);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

    if (err != 0)
	return err;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    err = pse51_syscall_init();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
    
    if (err != 0)
        {
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
	xncore_detach();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
        xnpod_shutdown(err);    
	return err;
        }

    pse51_reg_pkg_init(64, 128); /* FIXME: replace with compilation constants. */
    pse51_signal_pkg_init();
    pse51_mutex_pkg_init();
    pse51_sem_pkg_init();
    pse51_tsd_pkg_init();
    pse51_cond_pkg_init();
    pse51_mq_pkg_init();
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    pse51_intr_pkg_init();
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
    pse51_timer_pkg_init();
    pse51_shm_pkg_init();

    pse51_thread_pkg_init(module_param_value(time_slice_arg));

    return 0;
}

void SKIN_EXIT(posix)
{
    xnprintf("stopping POSIX services.\n");
    pse51_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__posix_skin_init);
module_exit(__posix_skin_exit);
