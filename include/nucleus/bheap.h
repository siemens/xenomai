/*
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>
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

#ifndef _XENO_NUCLEUS_BHEAP_H
#define _XENO_NUCLEUS_BHEAP_H

#include <nucleus/compiler.h>

/* Priority queue implementation, using a binary heap. */

typedef unsigned long long bheap_key_t;

typedef struct bheaph {
    bheap_key_t key;
    unsigned prio;
    unsigned pos;
} bheaph_t;

#define bheaph_init(holder) do { } while (0)
#define bheaph_key(holder)  ((holder)->key)
#define bheaph_prio(holder) ((holder)->prio)
#define bheaph_pos(holder)  ((holder)->pos)
#define bheaph_lt(h1, h2)   ((h1)->key < (h2)->key ||     \
                             ((h1)->key == (h2)->key &&   \
                              (h1)->prio > (h2)->prio))

typedef struct bheap {
    unsigned sz;
    unsigned last;
    bheaph_t **elems;
} bheap_t;

static inline bheaph_t *bheap_gethead(bheap_t *heap)
{
    if (heap->last == 1)
        return NULL;

    return heap->elems[1];
}

static inline bheaph_t *bheaph_parent(bheap_t *heap, bheaph_t *holder)
{
    const unsigned pos = holder->pos;

    return likely(pos > 1) ? heap->elems[pos / 2] : NULL;
}

static inline bheaph_t *bheaph_child(bheap_t *heap, bheaph_t *holder, int side)
{
    const unsigned pos = 2 * holder->pos + side;

    return likely(pos < heap->last) ? heap->elems[pos] : NULL;
}

static inline int bheap_init(bheap_t *heap, unsigned sz)
{
    heap->sz = sz;
    heap->last = 1;
    heap->elems = (bheaph_t **) xnarch_sysalloc(sz * sizeof(void *));

    if (!heap->elems)
        return ENOMEM;

    /* start indexing at 1. */
    heap->elems -= 1;

    return 0;
}

static inline void bheap_destroy(bheap_t *heap)
{    
    xnarch_sysfree(heap->elems + 1, heap->sz * sizeof(void *));
    heap->last = 0;
    heap->sz = 0;
}

static inline void bheap_swap(bheap_t *heap, bheaph_t *h1, bheaph_t *h2)
{
    const unsigned pos2 = bheaph_pos(h2);

    heap->elems[bheaph_pos(h1)] = h2;
    bheaph_pos(h2) = bheaph_pos(h1);
    heap->elems[pos2] = h1;
    bheaph_pos(h1) = pos2;
}

static inline void bheap_up(bheap_t *heap, bheaph_t *holder)
{
    bheaph_t *parent;

    while ((parent = bheaph_parent(heap, holder)) && bheaph_lt(holder, parent))
        bheap_swap(heap, holder, parent);
}

static inline void bheap_down(bheap_t *heap, bheaph_t *holder)
{
    bheaph_t *left, *right, *minchild;

    for (;;) {
        left = bheaph_child(heap, holder, 0);
        right = bheaph_child(heap, holder, 1);
        
        if (left && right)
            minchild = bheaph_lt(left, right) ? left : right;
        else
            minchild = left ?: right;

        if (!minchild || bheaph_lt(holder, minchild))
            break;

        bheap_swap(heap, minchild, holder);
    }
}

static inline int bheap_insert(bheap_t *heap, bheaph_t *holder)
{
    if (heap->last == heap->sz + 1)
        return EBUSY;

    heap->elems[heap->last] = holder;
    bheaph_pos(holder) = heap->last;
    ++heap->last;
    bheap_up(heap, holder);
    return 0;
}

static inline int bheap_delete(bheap_t *heap, bheaph_t *holder)
{
    bheaph_t *lasth;
    
    if (heap->last == 1)
        return EINVAL;
    
    --heap->last;
    if (heap->last > 1) {
        lasth = heap->elems[heap->last];
        heap->elems[bheaph_pos(holder)] = lasth;
        bheaph_pos(lasth) = bheaph_pos(holder);
        bheap_down(heap, lasth);
    }
   
    return 0;
}

static inline bheaph_t *bheap_get(bheap_t *heap)
{
    bheaph_t *holder = bheap_gethead(heap);

    if (!holder)
        return NULL;

    bheap_delete(heap, holder);

    return holder;
}

#endif /* _XENO_NUCLEUS_BHEAP_H */
