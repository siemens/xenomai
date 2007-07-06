/*
 * Copyright (C) 2006 Jan Kiszka <jan.kiszka@web.de>.
 * Copyright (C) 2006 Dmitry Adamushko <dmitry.adamushko@gmail.com>.
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

#ifndef _XENO_NUCLEUS_STAT_H
#define _XENO_NUCLEUS_STAT_H

#ifdef CONFIG_XENO_OPT_STATS

#include <nucleus/types.h>

typedef struct xnstat_runtime {

	xnticks_t start;   /* Start of execution time accumulation */

	xnticks_t total; /* Accumulated execution time */

} xnstat_runtime_t;

/* Return current date which can be passed to other xnstat services for
   immediate or lazy accounting. */
#define xnstat_runtime_now() xnarch_get_cpu_tsc()

/* Accumulate runtime of the current account until the given date. */
#define xnstat_runtime_update(sched, start) \
do { \
	(sched)->current_account->total += \
		start - (sched)->last_account_switch; \
	(sched)->last_account_switch = start; \
 	/* All changes must be committed before changing the current_account \
 	   reference in sched (required for xnintr_sync_stat_references) */ \
 	xnarch_memory_barrier(); \
} while (0)

/* Update the current account reference, returning the previous one. */
#define xnstat_runtime_set_current(sched, new_account) \
({ \
	xnstat_runtime_t *__prev; \
	__prev = xnarch_atomic_xchg(&(sched)->current_account, (new_account)); \
	__prev; \
})

/* Return the currently active accounting entity. */
#define xnstat_runtime_get_current(sched) ((sched)->current_account)

/* Finalize an account (no need to accumulate the runtime, just mark the
   switch date and set the new account). */
#define xnstat_runtime_finalize(sched, new_account) \
do { \
	(sched)->last_account_switch = xnarch_get_cpu_tsc(); \
	(sched)->current_account = (new_account); \
} while (0)

/* Reset statistics from inside the accounted entity (e.g. after CPU
   migration). */
#define xnstat_runtime_reset_stats(stat) \
do { \
	(stat)->total = 0; \
	(stat)->start = xnarch_get_cpu_tsc(); \
} while (0)


typedef struct xnstat_counter {
	int counter;
} xnstat_counter_t;

static inline int xnstat_counter_inc(xnstat_counter_t *c) {
	return c->counter++;
}

static inline int xnstat_counter_get(xnstat_counter_t *c) {
	return c->counter;
}

#else /* !CONFIG_XENO_OPT_STATS */
typedef struct xnstat_runtime {
} xnstat_runtime_t;

#define xnstat_runtime_now()                                 0
#define xnstat_runtime_update(sched, start)                  do { } while (0)
#define xnstat_runtime_set_current(sched, new_account)       ({ NULL; })
#define xnstat_runtime_get_current(sched)                    ({ NULL; })
#define xnstat_runtime_finalize(sched, new_account)          do { } while (0)
#define xnstat_runtime_reset_stats(account)                  do { } while (0)

typedef struct xnstat_counter {
} xnstat_counter_t;

static inline int xnstat_counter_inc(xnstat_counter_t *c) { return 0; }
static inline int xnstat_counter_get(xnstat_counter_t *c) { return 0; }
#endif /* CONFIG_XENO_OPT_STATS */

/* Account the runtime of the current account until now, switch to
   new_account, and return the previous one. */
#define xnstat_runtime_switch(sched, new_account) \
({ \
	xnstat_runtime_update(sched, xnstat_runtime_now()); \
	xnstat_runtime_set_current(sched, new_account); \
})

/* Account the runtime of the current account until given start time, switch
   to new_account, and return the previous one. */
#define xnstat_runtime_lazy_switch(sched, new_account, start) \
({ \
	xnstat_runtime_update(sched, start); \
	xnstat_runtime_set_current(sched, new_account); \
})

#endif /* !_XENO_NUCLEUS_STAT_H */
