#ifndef CB_LOCK_H
#define CB_LOCK_H

#include <asm/xenomai/atomic.h>

#ifndef __KERNEL__
typedef void xnthread_t;
#endif /* __KERNEL__ */

#define test_claimed(owner) ((long) (owner) & 1)
#define clear_claimed(owner) ((xnthread_t *) ((long) (owner) & ~1))
#define set_claimed(owner, bit) \
        ((xnthread_t *) ((long) clear_claimed(owner) | !!(bit)))

#ifdef XNARCH_HAVE_US_ATOMIC_CMPXCHG

static  __inline__ int __cb_try_read_lock(xnarch_atomic_t *lock)
{
	unsigned val = xnarch_atomic_get(lock);
	while (likely(val != -1)) {
		unsigned old = xnarch_atomic_cmpxchg(lock, val, val + 1);
		if (likely(old == val))
			return 0;
		val = old;
	}
	return -EBUSY;
}

static __inline__ void __cb_read_unlock(xnarch_atomic_t *lock)
{
	unsigned old, val = xnarch_atomic_get(lock);
	while (likely(val != -1)) {
		old = xnarch_atomic_cmpxchg(lock, val, val - 1);
		if (likely(old == val))
			return;
		val = old;
	}
}

static __inline__ int __cb_try_write_lock(xnarch_atomic_t *lock)
{
	unsigned old = xnarch_atomic_cmpxchg(lock, 0, -1);
	if (unlikely(old))
		return -EBUSY;
	return 0;
}

static __inline__ void __cb_force_write_lock(xnarch_atomic_t *lock)
{
	xnarch_atomic_set(lock, -1);
}

static __inline__ void __cb_write_unlock(xnarch_atomic_t *lock)
{
	xnarch_atomic_set(lock, 0);
}
#define DECLARE_CB_LOCK_FLAGS(name) struct { } name __attribute__((unused))
#define cb_try_read_lock(lock, flags) __cb_try_read_lock(lock)
#define cb_read_unlock(lock, flags) __cb_read_unlock(lock)
#define cb_try_write_lock(lock, flags) __cb_try_write_lock(lock)
#define cb_force_write_lock(lock, flags) __cb_force_write_lock(lock)
#define cb_write_unlock(lock, flags) __cb_write_unlock(lock)
#else /* !XNARCH_HAVE_US_ATOMIC_CMPXCHG */
#ifdef __KERNEL__
#define DECLARE_CB_LOCK_FLAGS(name) spl_t name
#define cb_try_read_lock(lock, flags) \
	({ xnlock_get_irqsave(&nklock, flags); 0; })
#define cb_read_unlock(lock, flags) xnlock_put_irqrestore(&nklock, flags)
#define cb_try_write_lock(lock, flags)  \
	({ xnlock_get_irqsave(&nklock, flags); 0; })
#define cb_force_write_lock(lock, flags)  \
	({ xnlock_get_irqsave(&nklock, flags); 0; })
#define cb_write_unlock(lock, flags) xnlock_put_irqrestore(&nklock, flags)
#else /* !__KERNEL__ */
#define DECLARE_CB_LOCK_FLAGS(name)
#define cb_try_read_lock(lock, flags) (0)
#define cb_read_unlock(lock, flags) do { } while (0)
#define cb_try_write_lock(lock, flags) (0)
#define cb_force_write_lock(lock, flags) do { } while (0)
#define cb_write_unlock(lock, flags) do { } while (0)
#endif /* !__KERNEL__ */
#endif /* !XNARCH_HAVE_US_ATOMIC_CMPXCHG */

#endif /* CB_LOCK_H */
