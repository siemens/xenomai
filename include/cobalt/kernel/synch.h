/*
 * @note Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 * \ingroup synch
 */

#ifndef _COBALT_KERNEL_SYNCH_H
#define _COBALT_KERNEL_SYNCH_H

#include <cobalt/kernel/types.h>
#include <asm/xenomai/atomic.h>

/* Creation flags */
#define XNSYNCH_FIFO    0x0
#define XNSYNCH_PRIO    0x1
#define XNSYNCH_NOPIP   0x0
#define XNSYNCH_PIP     0x2
#define XNSYNCH_DREORD  0x4
#define XNSYNCH_OWNER   0x8

#ifndef CONFIG_XENO_OPT_DEBUG_SYNCH_RELAX
#define CONFIG_XENO_OPT_DEBUG_SYNCH_RELAX 0
#endif /* CONFIG_XENO_OPT_DEBUG_SYNCH_RELAX */

#define XNSYNCH_FLCLAIM XN_HANDLE_SPARE3 /* Corresponding bit in fast lock */

/* Fast lock API */
static inline int xnsynch_fast_owner_check(atomic_long_t *fastlock,
					   xnhandle_t ownerh)
{
	return (xnhandle_mask_spare(atomic_long_read(fastlock)) == ownerh) ?
		0 : -EPERM;
}

static inline int xnsynch_fast_acquire(atomic_long_t *fastlock,
				       xnhandle_t new_ownerh)
{
	xnhandle_t h;

	h = atomic_long_cmpxchg(fastlock, XN_NO_HANDLE, new_ownerh);
	if (h != XN_NO_HANDLE) {
		if (xnhandle_mask_spare(h) == new_ownerh)
			return -EBUSY;

		return -EAGAIN;
	}

	return 0;
}

static inline int xnsynch_fast_release(atomic_long_t *fastlock,
				       xnhandle_t cur_ownerh)
{
	return (atomic_long_cmpxchg(fastlock, cur_ownerh, XN_NO_HANDLE) ==
		cur_ownerh);
}

#ifdef __KERNEL__

#include <cobalt/kernel/list.h>

#define XNSYNCH_CLAIMED 0x10	/* Claimed by other thread(s) w/ PIP */

#define XNSYNCH_FLCLAIM XN_HANDLE_SPARE3 /* Corresponding bit in fast lock */

/* Spare flags usable by upper interfaces */
#define XNSYNCH_SPARE0  0x01000000
#define XNSYNCH_SPARE1  0x02000000
#define XNSYNCH_SPARE2  0x04000000
#define XNSYNCH_SPARE3  0x08000000
#define XNSYNCH_SPARE4  0x10000000
#define XNSYNCH_SPARE5  0x20000000
#define XNSYNCH_SPARE6  0x40000000
#define XNSYNCH_SPARE7  0x80000000

/* Statuses */
#define XNSYNCH_DONE    0	/* Resource available / operation complete */
#define XNSYNCH_WAIT    1	/* Calling thread blocked -- start rescheduling */
#define XNSYNCH_RESCHED 2	/* Force rescheduling */

struct xnthread;
struct xnsynch;

typedef struct xnsynch {
	struct list_head link;	/** thread->claimq */
	int wprio;		/** wait prio in claimq */
	xnflags_t status;	 /** Status word */
	struct list_head pendq;	 /** Pending threads */
	struct xnthread *owner;	/** Thread which owns the resource */
	atomic_long_t *fastlock; /** Pointer to fast lock word */
	void (*cleanup)(struct xnsynch *synch); /* Cleanup handler */
} xnsynch_t;

#define xnsynch_test_flags(synch,flags)	testbits((synch)->status,flags)
#define xnsynch_set_flags(synch,flags)	setbits((synch)->status,flags)
#define xnsynch_clear_flags(synch,flags)	clrbits((synch)->status,flags)

#define xnsynch_for_each_sleeper(__pos, __synch)		\
	list_for_each_entry(__pos, &(__synch)->pendq, plink)

#define xnsynch_for_each_sleeper_safe(__pos, __tmp, __synch)	\
	list_for_each_entry_safe(__pos, __tmp, &(__synch)->pendq, plink)

static inline int xnsynch_pended_p(struct xnsynch *synch)
{
	return !list_empty(&synch->pendq);
}

static inline struct xnthread *xnsynch_owner(struct xnsynch *synch)
{
	return synch->owner;
}

#define xnsynch_fastlock(synch)		((synch)->fastlock)
#define xnsynch_fastlock_p(synch)	((synch)->fastlock != NULL)
#define xnsynch_owner_check(synch, thread) \
	xnsynch_fast_owner_check((synch)->fastlock, xnthread_handle(thread))

#define xnsynch_fast_is_claimed(fastlock) \
	xnhandle_test_spare(fastlock, XNSYNCH_FLCLAIM)
#define xnsynch_fast_set_claimed(fastlock, enable) \
	(((fastlock) & ~XNSYNCH_FLCLAIM) | ((enable) ? XNSYNCH_FLCLAIM : 0))
#define xnsynch_fast_mask_claimed(fastlock) ((fastlock & ~XNSYNCH_FLCLAIM))

void __xnsynch_fixup_rescnt(struct xnthread *thread);

struct xnthread *__xnsynch_transfer_ownership(struct xnsynch *synch,
					      struct xnthread *lastowner);
#if XENO_DEBUG(SYNCH_RELAX)

void xnsynch_detect_relaxed_owner(struct xnsynch *synch,
				  struct xnthread *sleeper);

void xnsynch_detect_claimed_relax(struct xnthread *owner);

#else /* !XENO_DEBUG(SYNCH_RELAX) */

static inline void xnsynch_detect_relaxed_owner(struct xnsynch *synch,
				  struct xnthread *sleeper)
{
}

static inline void xnsynch_detect_claimed_relax(struct xnthread *owner)
{
}

#endif /* !XENO_DEBUG(SYNCH_RELAX) */

void xnsynch_init(struct xnsynch *synch, xnflags_t flags,
		  atomic_long_t *fastlock);

#define xnsynch_destroy(synch)	xnsynch_flush(synch, XNRMID)

static inline void xnsynch_set_owner(struct xnsynch *synch,
				     struct xnthread *thread)
{
	synch->owner = thread;
}

static inline void xnsynch_register_cleanup(struct xnsynch *synch,
					    void (*handler)(struct xnsynch *))
{
	synch->cleanup = handler;
}

xnflags_t xnsynch_sleep_on(struct xnsynch *synch,
			   xnticks_t timeout,
			   xntmode_t timeout_mode);

struct xnthread *xnsynch_wakeup_one_sleeper(struct xnsynch *synch);

int xnsynch_wakeup_many_sleepers(struct xnsynch *synch, int nr);

void xnsynch_wakeup_this_sleeper(struct xnsynch *synch,
				 struct xnthread *sleeper);

xnflags_t xnsynch_acquire(struct xnsynch *synch,
			  xnticks_t timeout,
			  xntmode_t timeout_mode);

struct xnthread *xnsynch_peek_pendq(struct xnsynch *synch);

int xnsynch_flush(struct xnsynch *synch, xnflags_t reason);

void xnsynch_release_all_ownerships(struct xnthread *thread);

void xnsynch_requeue_sleeper(struct xnthread *thread);

void xnsynch_forget_sleeper(struct xnthread *thread);

#endif /* __KERNEL__ */

#endif /* !_COBALT_KERNEL_SYNCH_H_ */
