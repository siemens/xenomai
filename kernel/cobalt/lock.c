/**
 * Copyright (C) 2001-2012 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004,2005 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
 *
 * @ingroup nucleus
 * @defgroup lock SMP locking services.
 *
 * SMP locking services.
 * @{
 */
#include <linux/module.h>
#include <cobalt/kernel/lock.h>

DEFINE_XNLOCK(nklock);
#if defined(CONFIG_SMP) || XENO_DEBUG(XNLOCK)
EXPORT_SYMBOL_GPL(nklock);

void __xnlock_spin(int cpu, struct xnlock *lock /*, */ XNLOCK_DBG_CONTEXT_ARGS)
{
	unsigned int spin_limit;

	xnlock_dbg_prepare_spin(&spin_limit);

	while (atomic_cmpxchg(&lock->owner, ~0, cpu) != ~0)
		do {
			cpu_relax();
			xnlock_dbg_spinning(lock, cpu, &spin_limit /*, */
					    XNLOCK_DBG_PASS_CONTEXT);
		} while(atomic_read(&lock->owner) != ~0);
}
EXPORT_SYMBOL_GPL(__xnlock_spin);

#ifdef CONFIG_XENO_HW_OUTOFLINE_XNLOCK
int ___xnlock_get(struct xnlock *lock /*, */ XNLOCK_DBG_CONTEXT_ARGS)
{
	return ____xnlock_get(lock /* , */ XNLOCK_DBG_PASS_CONTEXT);
}
EXPORT_SYMBOL_GPL(___xnlock_get);

void ___xnlock_put(struct xnlock *lock /*, */ XNLOCK_DBG_CONTEXT_ARGS)
{
	____xnlock_put(lock /* , */ XNLOCK_DBG_PASS_CONTEXT);
}
EXPORT_SYMBOL_GPL(___xnlock_put);
#endif /* out of line xnlock */
#endif /* CONFIG_SMP || XENO_DEBUG(XNLOCK) */

#if XENO_DEBUG(XNLOCK)
DEFINE_PER_CPU(struct xnlockinfo, xnlock_stats);
EXPORT_PER_CPU_SYMBOL_GPL(xnlock_stats);
#endif

/*@}*/
