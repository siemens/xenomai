/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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

/**
 * @defgroup posix POSIX skin.
 *
 * Xenomai POSIX skin is an implementation of a small subset of the Single
 * Unix specification over Xenomai generic RTOS core.
 *
 * The following table gives equivalence between Alchemy API services
 * and POSIX services.
 *
 * <CENTER>
 * <TABLE>
 * <TR><TH>Native API services</TH> <TH>POSIX API services</TH></TR>
 * <TR><TD>@ref alarm</TD>          <TD>@ref posix_time</TD></TR>
 * <TR><TD>@ref cond</TD>           <TD>@ref posix_cond</TD></TR>
 * <TR><TD>@ref event</TD>          <TD>no direct equivalence, <BR>
 *                                      see @ref posix_cond</TD></TR>
 * <TR><TD>@ref alchemy_heap</TD>   <TD>@ref posix_shm</TD></TR>
 * <TR><TD>@ref interrupt</TD>      <TD>@ref posix_intr</TD></TR>
 * <TR><TD>@ref mutex</TD>          <TD>@ref posix_mutex</TD></TR>
 * <TR><TD>@ref pipe</TD>           <TD>no direct equivalence, <BR>
 *                                      see @ref posix_mq</TD></TR>
 * <TR><TD>@ref alchemy_queue</TD>  <TD>@ref posix_mq</TD></TR>
 * <TR><TD>@ref semaphore</TD>      <TD>@ref posix_sem</TD></TR>
 * <TR><TD>@ref task</TD>           <TD>@ref posix_thread</TD></TR>
 * <TR><TD>@ref alchemy_timer</TD>  <TD>@ref posix_time</TD></TR>
 * </TABLE>
 * </CENTER>
 *
 */

#ifdef __KERNEL__
#include <cobalt/syscall.h>
#include "apc.h"
#endif /* __KERNEL__ */
#include <cobalt/posix.h>
#include "internal.h"
#include "cond.h"
#include "mutex.h"
#include "sem.h"
#include "sig.h"
#include "thread.h"
#include "tsd.h"
#include "mq.h"
#include "timer.h"
#include "registry.h"
#include "shm.h"

MODULE_DESCRIPTION("POSIX/COBALT interface");
MODULE_AUTHOR("gilles.chanteperdrix@xenomai.org");
MODULE_LICENSE("GPL");

static void cobalt_shutdown(int xtype)
{
	cobalt_thread_pkg_cleanup();
#ifdef CONFIG_XENO_OPT_POSIX_SHM
	cobalt_shm_pkg_cleanup();
#endif /* CONFIG_XENO_OPT_POSIX_SHM */
	cobalt_timer_pkg_cleanup();
	cobalt_mq_pkg_cleanup();
	cobalt_cond_pkg_cleanup();
	cobalt_tsd_pkg_cleanup();
	cobalt_sem_pkg_cleanup();
	cobalt_mutex_pkg_cleanup();
	cobalt_signal_pkg_cleanup();
	cobalt_reg_pkg_cleanup();
#ifdef CONFIG_XENO_OPT_POSIX_INTR
	cobalt_intr_pkg_cleanup();
#endif /* CONFIG_XENO_OPT_POSIX_INTR */
#ifdef __KERNEL__
	cobalt_syscall_cleanup();
	cobalt_apc_pkg_cleanup();
#endif /* __KERNEL__ */
	xnpod_shutdown(xtype);
}

int SKIN_INIT(posix)
{
	int err;

	xnprintf("starting POSIX services.\n");

	err = xnpod_init();
	if (err != 0)
		goto fail;

#ifdef __KERNEL__
	err = cobalt_apc_pkg_init();
	if (err)
		goto fail_shutdown_pod;
	err = cobalt_syscall_init();
#endif /* __KERNEL__ */

	if (err != 0) {
#ifdef __KERNEL__
		cobalt_apc_pkg_cleanup();
#endif /* __KERNEL__ */
	fail_shutdown_pod:
		xnpod_shutdown(err);
	  fail:
		xnlogerr("POSIX skin init failed, code %d.\n", err);
		return err;
	}

	cobalt_reg_pkg_init(64, 128);	/* FIXME: replace with compilation constants. */
	cobalt_signal_pkg_init();
	cobalt_mutex_pkg_init();
	cobalt_sem_pkg_init();
	cobalt_tsd_pkg_init();
	cobalt_cond_pkg_init();
	cobalt_mq_pkg_init();
#ifdef CONFIG_XENO_OPT_POSIX_INTR
	cobalt_intr_pkg_init();
#endif /* CONFIG_XENO_OPT_POSIX_INTR */
	cobalt_timer_pkg_init();
#ifdef CONFIG_XENO_OPT_POSIX_SHM
	cobalt_shm_pkg_init();
#endif /* CONFIG_XENO_OPT_POSIX_SHM */

	cobalt_thread_pkg_init(CONFIG_XENO_OPT_RR_QUANTUM * 1000);

	return 0;
}

void SKIN_EXIT(posix)
{
	xnprintf("stopping POSIX services.\n");
	cobalt_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__posix_skin_init);
module_exit(__posix_skin_exit);
