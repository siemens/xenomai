/**
 * @file
 * This file is part of the Xenomai project.
 *
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>
 * Copyright (C) 2005 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/types.h>
#include <linux/err.h>
#include <cobalt/kernel/select.h>
#include <cobalt/uapi/syscall.h>
#include <asm/xenomai/syscall.h>
#include <rtdm/rtdm_driver.h>
#include "internal.h"
#include "thread.h"
#include "mutex.h"
#include "cond.h"
#include "mqueue.h"
#include "sem.h"
#include "signal.h"
#include "timer.h"
#include "monitor.h"
#include "clock.h"
#include "event.h"
#include "select.h"

int cobalt_muxid;

static struct xnshadow_ppd *cobalt_process_attach(void)
{
	struct cobalt_process *cc;

	cc = kmalloc(sizeof(*cc), GFP_KERNEL);
	if (cc == NULL)
		return ERR_PTR(-ENOSPC);

	INIT_LIST_HEAD(&cc->kqueues.condq);
	INIT_LIST_HEAD(&cc->kqueues.mutexq);
	INIT_LIST_HEAD(&cc->kqueues.semq);
	INIT_LIST_HEAD(&cc->kqueues.threadq);
	INIT_LIST_HEAD(&cc->kqueues.timerq);
	INIT_LIST_HEAD(&cc->kqueues.monitorq);
	INIT_LIST_HEAD(&cc->kqueues.eventq);
	INIT_LIST_HEAD(&cc->uqds);
	INIT_LIST_HEAD(&cc->usems);

	return &cc->ppd;
}

static void cobalt_process_detach(struct xnshadow_ppd *ppd)
{
	struct cobalt_process *cc;

	cc = container_of(ppd, struct cobalt_process, ppd);

	cobalt_sem_usems_cleanup(cc);
	cobalt_mq_uqds_cleanup(cc);
	cobalt_monitorq_cleanup(&cc->kqueues);
	cobalt_timerq_cleanup(&cc->kqueues);
	cobalt_semq_cleanup(&cc->kqueues);
	cobalt_mutexq_cleanup(&cc->kqueues);
	cobalt_condq_cleanup(&cc->kqueues);
	cobalt_eventq_cleanup(&cc->kqueues);

	kfree(cc);
}

static struct xnsyscall cobalt_syscalls[] = {
	SKINCALL_DEF(sc_cobalt_thread_create, cobalt_thread_create, init),
	SKINCALL_DEF(sc_cobalt_thread_setschedparam_ex, cobalt_thread_setschedparam_ex, conforming),
	SKINCALL_DEF(sc_cobalt_thread_getschedparam_ex, cobalt_thread_getschedparam_ex, any),
	SKINCALL_DEF(sc_cobalt_sched_yield, cobalt_sched_yield, primary),
	SKINCALL_DEF(sc_cobalt_thread_make_periodic, cobalt_thread_make_periodic_np, conforming),
	SKINCALL_DEF(sc_cobalt_thread_wait, cobalt_thread_wait_np, primary),
	SKINCALL_DEF(sc_cobalt_thread_set_mode, cobalt_thread_set_mode_np, primary),
	SKINCALL_DEF(sc_cobalt_thread_set_name, cobalt_thread_set_name_np, any),
	SKINCALL_DEF(sc_cobalt_thread_probe, cobalt_thread_probe_np, any),
	SKINCALL_DEF(sc_cobalt_thread_kill, cobalt_thread_kill, any),
	SKINCALL_DEF(sc_cobalt_thread_getstat, cobalt_thread_stat, any),
	SKINCALL_DEF(sc_cobalt_sem_init, cobalt_sem_init, any),
	SKINCALL_DEF(sc_cobalt_sem_destroy, cobalt_sem_destroy, any),
	SKINCALL_DEF(sc_cobalt_sem_post, cobalt_sem_post, any),
	SKINCALL_DEF(sc_cobalt_sem_wait, cobalt_sem_wait, primary),
	SKINCALL_DEF(sc_cobalt_sem_timedwait, cobalt_sem_timedwait, primary),
	SKINCALL_DEF(sc_cobalt_sem_trywait, cobalt_sem_trywait, primary),
	SKINCALL_DEF(sc_cobalt_sem_getvalue, cobalt_sem_getvalue, any),
	SKINCALL_DEF(sc_cobalt_sem_open, cobalt_sem_open, any),
	SKINCALL_DEF(sc_cobalt_sem_close, cobalt_sem_close, any),
	SKINCALL_DEF(sc_cobalt_sem_unlink, cobalt_sem_unlink, any),
	SKINCALL_DEF(sc_cobalt_sem_init_np, cobalt_sem_init_np, any),
	SKINCALL_DEF(sc_cobalt_sem_broadcast_np, cobalt_sem_broadcast_np, any),
	SKINCALL_DEF(sc_cobalt_clock_getres, cobalt_clock_getres, any),
	SKINCALL_DEF(sc_cobalt_clock_gettime, cobalt_clock_gettime, any),
	SKINCALL_DEF(sc_cobalt_clock_settime, cobalt_clock_settime, any),
	SKINCALL_DEF(sc_cobalt_clock_nanosleep, cobalt_clock_nanosleep, nonrestartable),
	SKINCALL_DEF(sc_cobalt_mutex_init, cobalt_mutex_init, any),
	SKINCALL_DEF(sc_cobalt_check_init, cobalt_mutex_check_init, any),
	SKINCALL_DEF(sc_cobalt_mutex_destroy, cobalt_mutex_destroy, any),
	SKINCALL_DEF(sc_cobalt_mutex_lock, cobalt_mutex_lock, primary),
	SKINCALL_DEF(sc_cobalt_mutex_timedlock, cobalt_mutex_timedlock, primary),
	SKINCALL_DEF(sc_cobalt_mutex_trylock, cobalt_mutex_trylock, primary),
	SKINCALL_DEF(sc_cobalt_mutex_unlock, cobalt_mutex_unlock, nonrestartable),
	SKINCALL_DEF(sc_cobalt_cond_init, cobalt_cond_init, any),
	SKINCALL_DEF(sc_cobalt_cond_destroy, cobalt_cond_destroy, any),
	SKINCALL_DEF(sc_cobalt_cond_wait_prologue, cobalt_cond_wait_prologue, nonrestartable),
	SKINCALL_DEF(sc_cobalt_cond_wait_epilogue, cobalt_cond_wait_epilogue, primary),
	SKINCALL_DEF(sc_cobalt_mq_open, cobalt_mq_open, lostage),
	SKINCALL_DEF(sc_cobalt_mq_close, cobalt_mq_close, lostage),
	SKINCALL_DEF(sc_cobalt_mq_unlink, cobalt_mq_unlink, lostage),
	SKINCALL_DEF(sc_cobalt_mq_getattr, cobalt_mq_getattr, any),
	SKINCALL_DEF(sc_cobalt_mq_setattr, cobalt_mq_setattr, any),
	SKINCALL_DEF(sc_cobalt_mq_send, cobalt_mq_send, primary),
	SKINCALL_DEF(sc_cobalt_mq_timedsend, cobalt_mq_timedsend, primary),
	SKINCALL_DEF(sc_cobalt_mq_receive, cobalt_mq_receive, primary),
	SKINCALL_DEF(sc_cobalt_mq_timedreceive, cobalt_mq_timedreceive, primary),
	SKINCALL_DEF(sc_cobalt_mq_notify, cobalt_mq_notify, primary),
	SKINCALL_DEF(sc_cobalt_sigwait, cobalt_sigwait, primary),
	SKINCALL_DEF(sc_cobalt_sigwaitinfo, cobalt_sigwaitinfo, primary),
	SKINCALL_DEF(sc_cobalt_sigtimedwait, cobalt_sigtimedwait, primary),
	SKINCALL_DEF(sc_cobalt_sigpending, cobalt_sigpending, primary),
	SKINCALL_DEF(sc_cobalt_timer_create, cobalt_timer_create, any),
	SKINCALL_DEF(sc_cobalt_timer_delete, cobalt_timer_delete, any),
	SKINCALL_DEF(sc_cobalt_timer_settime, cobalt_timer_settime, primary),
	SKINCALL_DEF(sc_cobalt_timer_gettime, cobalt_timer_gettime, any),
	SKINCALL_DEF(sc_cobalt_timer_getoverrun, cobalt_timer_getoverrun, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_init, cobalt_mutexattr_init, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_destroy, cobalt_mutexattr_destroy, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_gettype, cobalt_mutexattr_gettype, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_settype, cobalt_mutexattr_settype, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_getprotocol, cobalt_mutexattr_getprotocol, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_setprotocol, cobalt_mutexattr_setprotocol, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_getpshared, cobalt_mutexattr_getpshared, any),
	SKINCALL_DEF(sc_cobalt_mutexattr_setpshared, cobalt_mutexattr_setpshared, any),
	SKINCALL_DEF(sc_cobalt_condattr_init, cobalt_condattr_init, any),
	SKINCALL_DEF(sc_cobalt_condattr_destroy, cobalt_condattr_destroy, any),
	SKINCALL_DEF(sc_cobalt_condattr_getclock, cobalt_condattr_getclock, any),
	SKINCALL_DEF(sc_cobalt_condattr_setclock, cobalt_condattr_setclock, any),
	SKINCALL_DEF(sc_cobalt_condattr_getpshared, cobalt_condattr_getpshared, any),
	SKINCALL_DEF(sc_cobalt_condattr_setpshared, cobalt_condattr_setpshared, any),
	SKINCALL_DEF(sc_cobalt_select, cobalt_select, primary),
	SKINCALL_DEF(sc_cobalt_sched_minprio, cobalt_sched_min_prio, any),
	SKINCALL_DEF(sc_cobalt_sched_maxprio, cobalt_sched_max_prio, any),
	SKINCALL_DEF(sc_cobalt_monitor_init, cobalt_monitor_init, any),
	SKINCALL_DEF(sc_cobalt_monitor_destroy, cobalt_monitor_destroy, primary),
	SKINCALL_DEF(sc_cobalt_monitor_enter, cobalt_monitor_enter, primary),
	SKINCALL_DEF(sc_cobalt_monitor_wait, cobalt_monitor_wait, nonrestartable),
	SKINCALL_DEF(sc_cobalt_monitor_sync, cobalt_monitor_sync, nonrestartable),
	SKINCALL_DEF(sc_cobalt_monitor_exit, cobalt_monitor_exit, primary),
	SKINCALL_DEF(sc_cobalt_event_init, cobalt_event_init, any),
	SKINCALL_DEF(sc_cobalt_event_destroy, cobalt_event_destroy, any),
	SKINCALL_DEF(sc_cobalt_event_wait, cobalt_event_wait, primary),
	SKINCALL_DEF(sc_cobalt_event_sync, cobalt_event_sync, any),
	SKINCALL_DEF(sc_cobalt_sched_setconfig_np, cobalt_sched_setconfig_np, any),
};

struct xnpersonality cobalt_personality = {
	.name = "cobalt",
	.magic = COBALT_BINDING_MAGIC,
	.nrcalls = ARRAY_SIZE(cobalt_syscalls),
	.syscalls = cobalt_syscalls,
	.ops = {
		.attach_process = cobalt_process_attach,
		.detach_process = cobalt_process_detach,
		.exit_thread = cobalt_thread_exit,
		.finalize_thread = cobalt_thread_finalize,
	},
};
EXPORT_SYMBOL_GPL(cobalt_personality);

int cobalt_syscall_init(void)
{
	cobalt_muxid = xnshadow_register_personality(&cobalt_personality);
	if (cobalt_muxid < 0)
		return -ENOSYS;

	return 0;
}

void cobalt_syscall_cleanup(void)
{
	xnshadow_unregister_personality(cobalt_muxid);
}
