/**
 * @file
 * Copyright (C) 2001-2008,2012 Philippe Gerum <rpm@xenomai.org>.
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
 * \ingroup lock
 */

#ifndef _XENO_NUCLEUS_LOCK_H
#define _XENO_NUCLEUS_LOCK_H

#include <linux/ipipe.h>
#include <nucleus/assert.h>
#include <asm/xenomai/atomic.h>

/** \addtogroup lock
 *@{*/

typedef unsigned long spl_t;

#define splhigh(x)  ((x) = ipipe_test_and_stall_head() & 1)
#ifdef CONFIG_SMP
#define splexit(x)  ipipe_restore_head(x & 1)
#else /* !CONFIG_SMP */
#define splexit(x)  ipipe_restore_head(x)
#endif /* !CONFIG_SMP */
#define splmax()    ipipe_stall_head()
#define splnone()   ipipe_unstall_head()
#define spltest()   ipipe_test_head()

#ifndef CONFIG_XENO_OPT_DEBUG_XNLOCK
#define CONFIG_XENO_OPT_DEBUG_XNLOCK 0
#endif

#if XENO_DEBUG(XNLOCK)

struct xnlock {
	atomic_t owner;
	const char *file;
	const char *function;
	unsigned int line;
	int cpu;
	unsigned long long spin_time;
	unsigned long long lock_date;
};

struct xnlockinfo {
	unsigned long long spin_time;
	unsigned long long lock_time;
	const char *file;
	const char *function;
	unsigned int line;
};

#define XNARCH_LOCK_UNLOCKED (struct xnlock) {	\
	{ ~0 },					\
	NULL,					\
	NULL,					\
	0,					\
	-1,					\
	0LL,					\
	0LL,					\
}

#define XNLOCK_DBG_CONTEXT		, __FILE__, __LINE__, __FUNCTION__
#define XNLOCK_DBG_CONTEXT_ARGS					\
	, const char *file, int line, const char *function
#define XNLOCK_DBG_PASS_CONTEXT		, file, line, function

void xnlock_dbg_prepare_acquire(unsigned long long *start);
void xnlock_dbg_prepare_spin(unsigned int *spin_limit);
void xnlock_dbg_spinning(struct xnlock *lock, int cpu,
			 unsigned int *spin_limit,
			 const char *file, int line,
			 const char *function);
void xnlock_dbg_acquired(struct xnlock *lock, int cpu,
			 unsigned long long *start,
			 const char *file, int line,
			 const char *function);
int xnlock_dbg_release(struct xnlock *lock,
			 const char *file, int line,
			 const char *function);

#else /* !XENO_DEBUG(XNLOCK) */

struct xnlock {
	atomic_t owner;
};

#define XNARCH_LOCK_UNLOCKED		(struct xnlock) { { ~0 } }

#define XNLOCK_DBG_CONTEXT
#define XNLOCK_DBG_CONTEXT_ARGS
#define XNLOCK_DBG_PASS_CONTEXT

static inline
void xnlock_dbg_prepare_acquire(unsigned long long *start)
{
}

static inline
void xnlock_dbg_prepare_spin(unsigned int *spin_limit)
{
}

static inline void
xnlock_dbg_spinning(struct xnlock *lock, int cpu, unsigned int *spin_limit)
{
}

static inline void
xnlock_dbg_acquired(struct xnlock *lock, int cpu,
		    unsigned long long *start)
{
}

static inline int xnlock_dbg_release(struct xnlock *lock)
{
	return 0;
}

#endif /* !XENO_DEBUG(XNLOCK) */

#if defined(CONFIG_SMP) || XENO_DEBUG(XNLOCK)

#define xnlock_get(lock)		__xnlock_get(lock  XNLOCK_DBG_CONTEXT)
#define xnlock_put(lock)		__xnlock_put(lock  XNLOCK_DBG_CONTEXT)
#define xnlock_get_irqsave(lock,x) \
	((x) = __xnlock_get_irqsave(lock  XNLOCK_DBG_CONTEXT))
#define xnlock_put_irqrestore(lock,x) \
	__xnlock_put_irqrestore(lock,x  XNLOCK_DBG_CONTEXT)
#define xnlock_clear_irqoff(lock)	xnlock_put_irqrestore(lock, 1)
#define xnlock_clear_irqon(lock)	xnlock_put_irqrestore(lock, 0)

static inline void xnlock_init (struct xnlock *lock)
{
	*lock = XNARCH_LOCK_UNLOCKED;
}

#define DECLARE_XNLOCK(lock)		struct xnlock lock
#define DECLARE_EXTERN_XNLOCK(lock)	extern struct xnlock lock
#define DEFINE_XNLOCK(lock)		struct xnlock lock = XNARCH_LOCK_UNLOCKED
#define DEFINE_PRIVATE_XNLOCK(lock)	static DEFINE_XNLOCK(lock)

void __xnlock_spin(struct xnlock *lock /*, */ XNLOCK_DBG_CONTEXT_ARGS);

static inline int __xnlock_get(struct xnlock *lock /*, */ XNLOCK_DBG_CONTEXT_ARGS)
{
	int cpu = ipipe_processor_id();
	unsigned long long start;

	if (atomic_read(&lock->owner) == cpu)
		return 1;

	xnlock_dbg_prepare_acquire(&start);

	if (unlikely(atomic_cmpxchg(&lock->owner, ~0, cpu) != ~0))
		__xnlock_spin(lock /*, */ XNLOCK_DBG_PASS_CONTEXT);

	xnlock_dbg_acquired(lock, cpu, &start /*, */ XNLOCK_DBG_PASS_CONTEXT);

	return 0;
}

static inline void __xnlock_put(struct xnlock *lock /*, */ XNLOCK_DBG_CONTEXT_ARGS)
{
	if (xnlock_dbg_release(lock /*, */ XNLOCK_DBG_PASS_CONTEXT))
		return;

	/*
	 * Make sure all data written inside the lock is visible to
	 * other CPUs before we release the lock.
	 */
	mb();
	atomic_set(&lock->owner, ~0);
}

static inline spl_t
__xnlock_get_irqsave(struct xnlock *lock /*, */ XNLOCK_DBG_CONTEXT_ARGS)
{
	unsigned long flags;

	splhigh(flags);

	if (__xnlock_get(lock /*, */ XNLOCK_DBG_PASS_CONTEXT))
		flags |= 2;	/* Recursive acquisition */

	return flags;
}

static inline void __xnlock_put_irqrestore(struct xnlock *lock, spl_t flags
					   /*, */ XNLOCK_DBG_CONTEXT_ARGS)
{
	/* Only release the lock if we didn't take it recursively. */
	if (!(flags & 2))
		__xnlock_put(lock /*, */ XNLOCK_DBG_PASS_CONTEXT);

	splexit(flags & 1);
}

static inline int xnlock_is_owner(struct xnlock *lock)
{
	return atomic_read(&lock->owner) == ipipe_processor_id();
}

#else /* !(CONFIG_SMP || XENO_DEBUG(XNLOCK) */

#define xnlock_init(lock)		do { } while(0)
#define xnlock_get(lock)		do { } while(0)
#define xnlock_put(lock)		do { } while(0)
#define xnlock_get_irqsave(lock,x)	splhigh(x)
#define xnlock_put_irqrestore(lock,x)	splexit(x)
#define xnlock_clear_irqoff(lock)	splmax()
#define xnlock_clear_irqon(lock)	splnone()
#define xnlock_is_owner(lock)		1

#define DECLARE_XNLOCK(lock)
#define DECLARE_EXTERN_XNLOCK(lock)
#define DEFINE_XNLOCK(lock)
#define DEFINE_PRIVATE_XNLOCK(lock)

#endif /* !(CONFIG_SMP || XENO_DEBUG(XNLOCK)) */

/*@}*/

#endif /* !_XENO_NUCLEUS_LOCK_H */
