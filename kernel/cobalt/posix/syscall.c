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
#include <linux/slab.h>
#include <cobalt/kernel/select.h>
#include <cobalt/uapi/syscall.h>
#include <cobalt/kernel/tree.h>
#include <asm/xenomai/syscall.h>
#include <rtdm/driver.h>
#include <rtdm/fd.h>
#include "internal.h"
#include "thread.h"
#include "sched.h"
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
#include "timerfd.h"

int cobalt_muxid;

static void *cobalt_process_attach(void)
{
	struct cobalt_process *cc;

	cc = kmalloc(sizeof(*cc), GFP_KERNEL);
	if (cc == NULL)
		return ERR_PTR(-ENOSPC);

	INIT_LIST_HEAD(&cc->kqueues.condq);
	INIT_LIST_HEAD(&cc->kqueues.mutexq);
	INIT_LIST_HEAD(&cc->kqueues.semq);
	INIT_LIST_HEAD(&cc->kqueues.threadq);
	INIT_LIST_HEAD(&cc->kqueues.monitorq);
	INIT_LIST_HEAD(&cc->kqueues.eventq);
	INIT_LIST_HEAD(&cc->kqueues.schedq);
	INIT_LIST_HEAD(&cc->sigwaiters);
	xntree_init(&cc->usems);
	bitmap_fill(cc->timers_map, CONFIG_XENO_OPT_NRTIMERS);

	return cc;
}

static void cobalt_process_detach(void *arg)
{
	struct cobalt_process *cc = arg;

	cobalt_sem_usems_cleanup(cc);
	cobalt_timers_cleanup(cc);
	cobalt_monitorq_cleanup(&cc->kqueues);
	cobalt_semq_cleanup(&cc->kqueues);
	cobalt_mutexq_cleanup(&cc->kqueues);
	cobalt_condq_cleanup(&cc->kqueues);
	cobalt_eventq_cleanup(&cc->kqueues);
	cobalt_sched_cleanup(&cc->kqueues);

	kfree(cc);
}

static int cobalt_syscall_ni(void)
{
	return -ENOSYS;
}

static struct xnsyscall cobalt_syscalls[] = {
	[0 ... __NR_COBALT_SYSCALLS-1] = SKINCALL_NI,
	SKINCALL_DEF(sc_cobalt_thread_create, cobalt_thread_create, init),
	SKINCALL_DEF(sc_cobalt_thread_setschedparam_ex, cobalt_thread_setschedparam_ex, conforming),
	SKINCALL_DEF(sc_cobalt_thread_getschedparam_ex, cobalt_thread_getschedparam_ex, current),
	SKINCALL_DEF(sc_cobalt_sched_weightprio, cobalt_sched_weighted_prio, current),
	SKINCALL_DEF(sc_cobalt_sched_yield, cobalt_sched_yield, primary),
	SKINCALL_DEF(sc_cobalt_thread_make_periodic, cobalt_thread_make_periodic_np, conforming),
	SKINCALL_DEF(sc_cobalt_thread_wait, cobalt_thread_wait_np, primary),
	SKINCALL_DEF(sc_cobalt_thread_set_mode, cobalt_thread_set_mode_np, primary),
	SKINCALL_DEF(sc_cobalt_thread_set_name, cobalt_thread_set_name_np, current),
	SKINCALL_DEF(sc_cobalt_thread_kill, cobalt_thread_kill, conforming),
	SKINCALL_DEF(sc_cobalt_thread_getstat, cobalt_thread_stat, current),
	SKINCALL_DEF(sc_cobalt_thread_join, cobalt_thread_join, primary),
	SKINCALL_DEF(sc_cobalt_sem_init, cobalt_sem_init, current),
	SKINCALL_DEF(sc_cobalt_sem_destroy, cobalt_sem_destroy, current),
	SKINCALL_DEF(sc_cobalt_sem_post, cobalt_sem_post, current),
	SKINCALL_DEF(sc_cobalt_sem_wait, cobalt_sem_wait, primary),
	SKINCALL_DEF(sc_cobalt_sem_timedwait, cobalt_sem_timedwait, primary),
	SKINCALL_DEF(sc_cobalt_sem_trywait, cobalt_sem_trywait, primary),
	SKINCALL_DEF(sc_cobalt_sem_getvalue, cobalt_sem_getvalue, current),
	SKINCALL_DEF(sc_cobalt_sem_open, cobalt_sem_open, current),
	SKINCALL_DEF(sc_cobalt_sem_close, cobalt_sem_close, current),
	SKINCALL_DEF(sc_cobalt_sem_unlink, cobalt_sem_unlink, current),
	SKINCALL_DEF(sc_cobalt_sem_init_np, cobalt_sem_init_np, current),
	SKINCALL_DEF(sc_cobalt_sem_broadcast_np, cobalt_sem_broadcast_np, current),
	SKINCALL_DEF(sc_cobalt_sem_inquire, cobalt_sem_inquire, current),
	SKINCALL_DEF(sc_cobalt_clock_getres, cobalt_clock_getres, current),
	SKINCALL_DEF(sc_cobalt_clock_gettime, cobalt_clock_gettime, current),
	SKINCALL_DEF(sc_cobalt_clock_settime, cobalt_clock_settime, current),
	SKINCALL_DEF(sc_cobalt_clock_nanosleep, cobalt_clock_nanosleep, nonrestartable),
	SKINCALL_DEF(sc_cobalt_mutex_init, cobalt_mutex_init, current),
	SKINCALL_DEF(sc_cobalt_mutex_check_init, cobalt_mutex_check_init, current),
	SKINCALL_DEF(sc_cobalt_mutex_destroy, cobalt_mutex_destroy, current),
	SKINCALL_DEF(sc_cobalt_mutex_lock, cobalt_mutex_lock, primary),
	SKINCALL_DEF(sc_cobalt_mutex_timedlock, cobalt_mutex_timedlock, primary),
	SKINCALL_DEF(sc_cobalt_mutex_trylock, cobalt_mutex_trylock, primary),
	SKINCALL_DEF(sc_cobalt_mutex_unlock, cobalt_mutex_unlock, nonrestartable),
	SKINCALL_DEF(sc_cobalt_cond_init, cobalt_cond_init, current),
	SKINCALL_DEF(sc_cobalt_cond_destroy, cobalt_cond_destroy, current),
	SKINCALL_DEF(sc_cobalt_cond_wait_prologue, cobalt_cond_wait_prologue, nonrestartable),
	SKINCALL_DEF(sc_cobalt_cond_wait_epilogue, cobalt_cond_wait_epilogue, primary),
	SKINCALL_DEF(sc_cobalt_mq_open, cobalt_mq_open, lostage),
	SKINCALL_DEF(sc_cobalt_mq_close, cobalt_mq_close, lostage),
	SKINCALL_DEF(sc_cobalt_mq_unlink, cobalt_mq_unlink, lostage),
	SKINCALL_DEF(sc_cobalt_mq_getattr, cobalt_mq_getattr, current),
	SKINCALL_DEF(sc_cobalt_mq_setattr, cobalt_mq_setattr, current),
	SKINCALL_DEF(sc_cobalt_mq_timedsend, cobalt_mq_timedsend, primary),
	SKINCALL_DEF(sc_cobalt_mq_timedreceive, cobalt_mq_timedreceive, primary),
	SKINCALL_DEF(sc_cobalt_mq_notify, cobalt_mq_notify, primary),
	SKINCALL_DEF(sc_cobalt_sigwait, cobalt_sigwait, primary),
	SKINCALL_DEF(sc_cobalt_sigwaitinfo, cobalt_sigwaitinfo, nonrestartable),
	SKINCALL_DEF(sc_cobalt_sigtimedwait, cobalt_sigtimedwait, nonrestartable),
	SKINCALL_DEF(sc_cobalt_sigpending, cobalt_sigpending, primary),
	SKINCALL_DEF(sc_cobalt_kill, cobalt_kill, conforming),
	SKINCALL_DEF(sc_cobalt_sigqueue, cobalt_sigqueue, conforming),
	SKINCALL_DEF(sc_cobalt_timer_create, cobalt_timer_create, current),
	SKINCALL_DEF(sc_cobalt_timer_delete, cobalt_timer_delete, current),
	SKINCALL_DEF(sc_cobalt_timer_settime, cobalt_timer_settime, primary),
	SKINCALL_DEF(sc_cobalt_timer_gettime, cobalt_timer_gettime, current),
	SKINCALL_DEF(sc_cobalt_timer_getoverrun, cobalt_timer_getoverrun, current),
	SKINCALL_DEF(sc_cobalt_timerfd_create, cobalt_timerfd_create, lostage),
	SKINCALL_DEF(sc_cobalt_timerfd_gettime, cobalt_timerfd_gettime, current),
	SKINCALL_DEF(sc_cobalt_timerfd_settime, cobalt_timerfd_settime, current),
	SKINCALL_DEF(sc_cobalt_select, cobalt_select, nonrestartable),
	SKINCALL_DEF(sc_cobalt_sched_minprio, cobalt_sched_min_prio, current),
	SKINCALL_DEF(sc_cobalt_sched_maxprio, cobalt_sched_max_prio, current),
	SKINCALL_DEF(sc_cobalt_monitor_init, cobalt_monitor_init, current),
	SKINCALL_DEF(sc_cobalt_monitor_destroy, cobalt_monitor_destroy, primary),
	SKINCALL_DEF(sc_cobalt_monitor_enter, cobalt_monitor_enter, primary),
	SKINCALL_DEF(sc_cobalt_monitor_wait, cobalt_monitor_wait, nonrestartable),
	SKINCALL_DEF(sc_cobalt_monitor_sync, cobalt_monitor_sync, nonrestartable),
	SKINCALL_DEF(sc_cobalt_monitor_exit, cobalt_monitor_exit, primary),
	SKINCALL_DEF(sc_cobalt_event_init, cobalt_event_init, current),
	SKINCALL_DEF(sc_cobalt_event_destroy, cobalt_event_destroy, current),
	SKINCALL_DEF(sc_cobalt_event_wait, cobalt_event_wait, primary),
	SKINCALL_DEF(sc_cobalt_event_sync, cobalt_event_sync, current),
	SKINCALL_DEF(sc_cobalt_event_inquire, cobalt_event_inquire, current),
	SKINCALL_DEF(sc_cobalt_sched_setconfig_np, cobalt_sched_setconfig_np, current),
	SKINCALL_DEF(sc_cobalt_sched_getconfig_np, cobalt_sched_getconfig_np, current),
};

struct xnpersonality cobalt_personality = {
	.name = "cobalt",
	.magic = COBALT_BINDING_MAGIC,
	.nrcalls = ARRAY_SIZE(cobalt_syscalls),
	.syscalls = cobalt_syscalls,
	.ops = {
		.attach_process = cobalt_process_attach,
		.detach_process = cobalt_process_detach,
		.map_thread = cobalt_thread_map,
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
