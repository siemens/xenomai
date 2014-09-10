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

#include <linux/init.h>
#include <linux/module.h>
#include "internal.h"
#include "thread.h"
#include "sched.h"
#include "cond.h"
#include "mutex.h"
#include "sem.h"
#include "mqueue.h"
#include "signal.h"
#include "timer.h"
#include "monitor.h"
#include "event.h"

struct cobalt_kqueues cobalt_global_kqueues;

void cobalt_cleanup(void)
{
	cobalt_process_cleanup();
	cobalt_monitor_pkg_cleanup();
	cobalt_event_pkg_cleanup();
	cobalt_signal_pkg_cleanup();
	cobalt_mq_pkg_cleanup();
	cobalt_sem_pkg_cleanup();
	cobalt_cond_pkg_cleanup();
	cobalt_mutex_pkg_cleanup();
	cobalt_sched_pkg_cleanup();
}

int __init cobalt_init(void)
{
	int ret;

	INIT_LIST_HEAD(&cobalt_global_kqueues.threadq);
	cobalt_time_slice = CONFIG_XENO_OPT_RR_QUANTUM * 1000;

	ret = cobalt_process_init();
	if (ret)
		return ret;

	cobalt_sched_pkg_init();
	cobalt_mutex_pkg_init();
	cobalt_sem_pkg_init();
	cobalt_cond_pkg_init();
	cobalt_signal_pkg_init();
	cobalt_mq_pkg_init();
	cobalt_event_pkg_init();
	cobalt_monitor_pkg_init();

	return 0;
}
