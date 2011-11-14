#ifndef ATOMIC_H
#define ATOMIC_H

#include <asm/xenomai/features.h>

typedef unsigned long atomic_flags_t;

#ifdef __KERNEL__
#include <linux/bitops.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/xenomai/wrappers.h>

typedef atomic_long_t atomic_counter_t;
typedef atomic_long_t xnarch_atomic_t;

#define xnarch_memory_barrier()		smp_mb()
#define xnarch_read_memory_barrier()    rmb()
#define xnarch_write_memory_barrier()   wmb()

#define xnarch_atomic_set(pcounter,i)	atomic_long_set(pcounter,i)
#define xnarch_atomic_get(pcounter)	atomic_long_read(pcounter)
#define xnarch_atomic_inc(pcounter)	atomic_long_inc(pcounter)
#define xnarch_atomic_dec(pcounter)	atomic_long_dec(pcounter)
#define xnarch_atomic_inc_and_test(pcounter) \
	atomic_long_inc_and_test(pcounter)
#define xnarch_atomic_dec_and_test(pcounter) \
	atomic_long_dec_and_test(pcounter)
#define xnarch_atomic_cmpxchg(pcounter,old,new) \
	atomic_long_cmpxchg((pcounter),(old),(new))

#define xnarch_atomic_xchg(ptr,x)	xchg(ptr,x)

/* atomic_set_mask, atomic_clear_mask are not standard among linux
   ports */
#ifndef xnarch_atomic_set_mask
#define xnarch_atomic_set_mask(pflags,mask) atomic_set_mask((mask),(pflags))
#endif

#ifndef xnarch_atomic_clear_mask
#define xnarch_atomic_clear_mask(pflags,mask) atomic_clear_mask((mask),(pflags))
#endif

#else /* !__KERNEL__ */
#include <xeno_config.h>

#ifndef xnarch_atomic_t
typedef struct { unsigned long counter; } __xnarch_atomic_t;
#define xnarch_atomic_t __xnarch_atomic_t
#endif

#ifndef xnarch_memory_barrier
#define xnarch_memory_barrier() __sync_synchronize()
#endif

#ifndef xnarch_read_memory_barrier
#define xnarch_read_memory_barrier() xnarch_memory_barrier()
#endif

#ifndef xnarch_write_memory_barrier
#define xnarch_write_memory_barrier() xnarch_memory_barrier()
#endif

#ifndef cpu_relax
#define cpu_relax() xnarch_memory_barrier()
#endif

#ifndef xnarch_atomic_get
#define xnarch_atomic_get(v)		((v)->counter)
#endif

#ifndef xnarch_atomic_set
#define xnarch_atomic_set(v,i)		(((v)->counter) = (i))
#endif

#ifndef xnarch_atomic_cmpxchg
#define xnarch_atomic_cmpxchg(v, o, n)			\
	__sync_val_compare_and_swap(&(v)->counter,	\
				    (unsigned long)(o), \
				    (unsigned long)(n))
#endif

#endif /* !__KERNEL__ */

#endif /* ATOMIC_H */
