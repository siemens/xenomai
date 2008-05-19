#ifndef _XENO_ASM_GENERIC_ATOMIC_H
#define _XENO_ASM_GENERIC_ATOMIC_H

typedef xnarch_atomic_t xnarch_atomic_intptr_t;

static inline void *xnarch_atomic_intptr_get(xnarch_atomic_intptr_t *l)
{
        xnarch_atomic_t *v = (xnarch_atomic_t *)l;

        return (void *)xnarch_atomic_get(v);
}

static inline void xnarch_atomic_intptr_set(xnarch_atomic_intptr_t *l, void *i)
{
        xnarch_atomic_t *v = (xnarch_atomic_t *)l;

        xnarch_atomic_set(v, (long)i);
}

#define xnarch_atomic_intptr_cmpxchg(l, old, new) \
        (void *)(xnarch_atomic_cmpxchg((xnarch_atomic_t *)(l), \
				       (long)(old), (long)(new)))

#endif /* _XENO_ASM_GENERIC_ATOMIC_H */
