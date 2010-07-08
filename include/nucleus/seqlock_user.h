#ifndef __SEQLOCK_USER_H
#define __SEQLOCK_USER_H

/* Originally from the linux kernel, adapted for userland and Xenomai */

#include <asm/xenomai/atomic.h>

typedef struct seqcount {
	unsigned sequence;
} seqcount_t;

#define SEQCNT_ZERO { 0 }
#define seqcount_init(x) do { *(x) = (seqcount_t) SEQCNT_ZERO; } while (0)

/* Start of read using pointer to a sequence counter only.  */
static inline unsigned read_seqcount_begin(const seqcount_t *s)
{
	unsigned ret;

repeat:
	ret = s->sequence;
	xnarch_read_memory_barrier();
	if (unlikely(ret & 1)) {
		cpu_relax();
		goto repeat;
	}
	return ret;
}

/*
 * Test if reader processed invalid data because sequence number has changed.
 */
static inline int read_seqcount_retry(const seqcount_t *s, unsigned start)
{
	xnarch_read_memory_barrier();

	return s->sequence != start;
}


/*
 * The sequence counter only protects readers from concurrent writers.
 * Writers must use their own locking.
 */
static inline void write_seqcount_begin(seqcount_t *s)
{
	s->sequence++;
	xnarch_write_memory_barrier();
}

static inline void write_seqcount_end(seqcount_t *s)
{
	xnarch_write_memory_barrier();
	s->sequence++;
}

#endif
