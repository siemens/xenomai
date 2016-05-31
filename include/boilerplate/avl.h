/*
 * Copyright (c) 2015 Gilles Chanteperdrix
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef _BOILERPLATE_AVL_H
#define _BOILERPLATE_AVL_H

#include <stdlib.h>

struct avlh {
	unsigned int thr: 3;
	int type: 2;
	int balance :2;
	unsigned int flags :25;		/* Application-specific */
	struct avlh *link[3];
};

/* Using -1 and 1 for left and right is slightly faster than 0 and 1, using 0
   for "up" is just here for orthogonality... and avoid wasting 4 bytes or
   having to use a union in struct avlh. */
#define AVL_LEFT             -1
#define AVL_UP                0
#define AVL_RIGHT             1
/* maps AVL_LEFT to AVL_RIGHT and reciprocally. */
#define avl_opposite(type)   (-(type))
/* maps AVL_LEFT to -1 and AVL_RIGHT to 1. */
#define avl_type2sign(type)  (type)
/* maps AVL_LEFT and AVL_RIGHT to arrays index (or bit positions). */
#define avl_type2index(type) ((type)+1)
/* maps <0 to AVL_LEFT and >0 to AVL_RIGHT. */
#define avl_sign2type(sign)  (sign)

#define AVL_THR_LEFT  (1<<avl_type2index(AVL_LEFT))
#define AVL_THR_RIGHT (1<<avl_type2index(AVL_RIGHT))

#define avlh_thr_set(holder, side) ((holder)->thr |= 1 << avl_type2index(side))
#define avlh_thr_clr(holder, side) ((holder)->thr &= ~(1 << avl_type2index(side)))
#define avlh_thr_tst(holder, side) ((holder)->thr & (1 << avl_type2index(side)))
#define avlh_link(holder, dir)     ((holder)->link[avl_type2index(dir)])
#define avlh_up(holder)            avlh_link((holder), AVL_UP)
#define avlh_left(holder)          avlh_link((holder), AVL_LEFT)
#define avlh_right(holder)         avlh_link((holder), AVL_RIGHT)
#define avlh_parent_link(holder)   (avlh_link(avlh_up(holder), (holder)->type))

struct avl;

typedef struct avlh *avl_search_t(const struct avl *, const struct avlh *, int *);

typedef int avlh_cmp_t(const struct avlh *const, const struct avlh *const);

struct avl {
	struct avlh anchor;
	avl_search_t *search;
	avlh_cmp_t *cmp;
	struct avlh *end[3];
	unsigned int count;
	unsigned int height;
};

#define avl_searchfn(avl) ((avl)->search)
#define avl_cmp(avl)      ((avl)->cmp)
#define avl_count(avl)    ((avl)->count)
#define avl_height(avl)   ((avl)->height)
#define avl_anchor(avl)   (&(avl)->anchor)
#define avl_end(avl, dir) ((avl)->end[avl_type2index(dir)])
#define avl_top(avl)      (avlh_right(avl_anchor(avl)))
#define avl_head(avl)     (avl_end((avl), AVL_LEFT))
#define avl_tail(avl)     (avl_end((avl), AVL_RIGHT))

#ifdef __cplusplus
extern "C" {
#endif

void avl_init(struct avl *avl, avl_search_t *search, avlh_cmp_t *cmp);

void avl_destroy(struct avl *avl);

void avl_clear(struct avl *avl, void (*destruct)(struct avlh *));

int avl_insert(struct avl *avl, struct avlh *holder);

int avl_prepend(struct avl *avl, struct avlh *holder);

int avl_append(struct avl *avl, struct avlh *holder);

struct avlh *avl_update(struct avl *avl, struct avlh *holder);

struct avlh *avl_set(struct avl *avl, struct avlh *holder);

int avl_delete(struct avl *avl, struct avlh *node);

static inline struct avlh *avl_gettop(struct avl *const avl)
{
	struct avlh *const holder = avl_top(avl);

	if (holder != avl_anchor(avl))
		return holder;

	return NULL;
}

static inline struct avlh *avl_gethead(struct avl *const avl)
{
	struct avlh *const holder = avl_head(avl);

	if (holder != avl_anchor(avl))
		return holder;

	return NULL;
}

static inline struct avlh *avl_gettail(struct avl *const avl)
{
	struct avlh *const holder = avl_tail(avl);

	if (holder != avl_anchor(avl))
		return holder;

	return NULL;
}

static inline unsigned avl_getcount(struct avl *const avl)
{
	return avl_count(avl);
}

static inline struct avlh *avl_inorder(struct avl *const avl,
				       struct avlh *const holder,
				       const int dir)
{
	/* Assume dir == AVL_RIGHT in comments. */
	struct avlh *child = avlh_link(holder, dir);

	/* If the current node is not right threaded, then go down left, starting
	   from its right child. */
	if (!avlh_thr_tst(holder, dir)) {
		const int opp_dir = avl_opposite(dir);
		while (!avlh_thr_tst(child, opp_dir))
			child = avlh_link(child, opp_dir);
	} else
		/* Else follow its right thread. */
		if (child != avl_anchor(avl))
			return child;
		else
			return NULL;

	return child;
}

static inline struct avlh *avl_postorder(struct avl *const avl,
					 struct avlh *const holder, const int dir)
{
	/* Assume dir == AVL_RIGHT in comments. */
	struct avlh *next = avlh_up(holder);

	if (holder->type != dir)
		/* If the current node is not a right node, follow the nodes in inorder
		   until we find a right threaded node. */
		while (!avlh_thr_tst(next, dir))
			next = avl_inorder(avl, next, dir);
	else
		/* else the current node is a right node, its parent is the next in
		   postorder. */
		if (next != avl_anchor(avl))
			return next;
		else
			return NULL;

	return next;
}

static inline struct avlh *avl_preorder(struct avl *const avl,
					struct avlh *holder, const int dir)
{
	struct avlh *next;
	/* Assume dir == AVL_RIGHT in comments. */
	/* If the current node has a left child (hence is not left threaded), then
	   return it. */
	if (!avlh_thr_tst(holder, avl_opposite(dir)))
		return avlh_link(holder, avl_opposite(dir));

	/* Else follow the right threads until we find a node which is not right
	   threaded (hence has a right child) and return its right child. */
	next = holder;

	while (avlh_thr_tst(next, dir)) {
		next = avlh_link(next, dir);
		if (next == avl_anchor(avl))
			goto ret_null;
	}

	return avlh_link(next, dir);
ret_null:
	return NULL;
}

/**
 * Get next node in symmetrical a.k.a inorder ordering.
 */
static inline struct avlh *avl_next(struct avl *const avl, struct avlh *const holder)
{
	return avl_inorder(avl, holder, AVL_RIGHT);
}

/**
 * Get previous node in symmetrical a.k.a inorder ordering.
 */
static inline struct avlh *avl_prev(struct avl *const avl, struct avlh *const holder)
{
	return avl_inorder(avl, holder, AVL_LEFT);
}

static inline struct avlh *avl_postorder_next(struct avl *const avl, struct avlh *const holder)
{
	return avl_postorder(avl, holder, AVL_RIGHT);
}

static inline struct avlh *avl_postorder_prev(struct avl *const avl, struct avlh *const holder)
{
	return avl_postorder(avl, holder, AVL_LEFT);
}

static inline struct avlh *avl_preorder_next(struct avl *const avl, struct avlh *const holder)
{
	return avl_preorder(avl, holder, AVL_RIGHT);
}

static inline struct avlh *avl_preorder_prev(struct avl *const avl, struct avlh *const holder)
{
	return avl_preorder(avl, holder, AVL_LEFT);
}

static inline void avlh_init(struct avlh *const holder)
{
	*(unsigned long *)holder = 0UL; /* Please valgrind */
	holder->thr = AVL_THR_LEFT | AVL_THR_RIGHT;
	holder->balance = 0;
	holder->type = 0;
}

static inline struct avlh *avl_search(struct avl *const avl, const struct avlh *node)
{
	struct avlh *holder;
	int delta;

	holder = avl_searchfn(avl)(avl, node, &delta);
	if (!delta)
		return holder;

	return NULL;
}

#ifdef __cplusplus
}
#endif

/**
 * Search a node, return its parent if it could not be found.
 */
#define DECLARE_AVL_SEARCH(avl_search_inner, cmp)                       \
static struct avlh *avl_search_inner(const struct avl *const avl,	\
				     const struct avlh *const node,	\
				     int *const pdelta)			\
{									\
	int delta = avl_type2sign(AVL_RIGHT);				\
	struct avlh *holder = avl_top(avl);				\
									\
	if (holder != avl_anchor(avl)) {				\
		while ((delta = cmp(holder, node))) {			\
			delta = delta < 0 ? -1 : 1;			\
			if (avlh_thr_tst(holder,avl_sign2type(delta)))	\
				break;					\
			holder = avlh_link(holder, avl_sign2type(delta)); \
		}							\
	}								\
	*pdelta = delta;						\
	return holder;							\
}									\

#endif /* !_BOILERPLATE_AVL_H */
