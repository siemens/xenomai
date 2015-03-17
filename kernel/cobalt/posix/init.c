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

LIST_HEAD(cobalt_thread_list);

struct cobalt_resources cobalt_global_resources = {
	.condq = LIST_HEAD_INIT(cobalt_global_resources.condq),
	.mutexq = LIST_HEAD_INIT(cobalt_global_resources.mutexq),
	.semq = LIST_HEAD_INIT(cobalt_global_resources.semq),
	.monitorq = LIST_HEAD_INIT(cobalt_global_resources.monitorq),
	.eventq = LIST_HEAD_INIT(cobalt_global_resources.eventq),
	.schedq = LIST_HEAD_INIT(cobalt_global_resources.schedq),
};

__init int cobalt_init(void)
{
	cobalt_time_slice = CONFIG_XENO_OPT_RR_QUANTUM * 1000;

	return cobalt_process_init();
}
