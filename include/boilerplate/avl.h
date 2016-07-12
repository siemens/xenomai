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

#include <stddef.h>
#include <stdio.h>

struct avlh {
#define AVLH_APP_BITS 28
	unsigned int flags: AVLH_APP_BITS;
	int type: 2;
	int balance: 2;
	union {
		ptrdiff_t offset;
		struct avlh *ptr;
	} link[3];
};

struct avl;

/*
 * Comparison function: should return -1 if left is less than right, 0
 * if they are equal and 1 if left is greather than right. You can use
 * the avl_sign function which will convert a difference to -1, 0,
 * 1. Beware of overflow however. You can also use avl_cmp_sign()
 * which should not have any such problems.
 */
typedef int avlh_cmp_t(const struct avlh *const, const struct avlh *const);

typedef struct avlh *
avl_search_t(const struct avl *, const struct avlh *, int *, int);

typedef int avlh_prn_t(char *, size_t, const struct avlh *const);

struct avl {
	struct avlh anchor;
	avl_search_t *search;
	avlh_cmp_t *cmp;
	union {
		ptrdiff_t offset;
		struct avlh *ptr;
	} end[3];
	unsigned int count;
	unsigned int height;
};

#define AVL_LEFT	     -1
#define AVL_UP		      0
#define AVL_RIGHT	      1
/* maps AVL_LEFT to AVL_RIGHT and reciprocally. */
#define avl_opposite(type)   (-(type))
/* maps AVL_LEFT and AVL_RIGHT to arrays index (or bit positions). */
#define avl_type2index(type) ((type)+1)

#define AVL_THR_LEFT  (1 << avl_type2index(AVL_LEFT))
#define AVL_THR_RIGHT (1 << avl_type2index(AVL_RIGHT))

#define avlh_up(avl, holder)	avlh_link((avl), (holder), AVL_UP)
#define avlh_left(avl, holder)	avlh_link((avl), (holder), AVL_LEFT)
#define avlh_right(avl, holder)	avlh_link((avl), (holder), AVL_RIGHT)

#define avlh_thr_tst(avl, holder, side) (avlh_link(avl, holder, side) == NULL)
#define avlh_child(avl, holder, side) (avlh_link((avl),(holder),(side)))
#define avlh_has_child(avl, holder, side) (!avlh_thr_tst(avl, holder, side))

#define avl_searchfn(avl) ((avl)->search)
#define avl_cmp(avl)	  ((avl)->cmp)
#define avl_count(avl)	  ((avl)->count)
#define avl_height(avl)	  ((avl)->height)
#define avl_anchor(avl)	  (&(avl)->anchor)
#define avl_top(avl)	  (avlh_right(avl, avl_anchor(avl)))
#define avl_head(avl)	  (avl_end((avl), AVL_LEFT))
#define avl_tail(avl)	  (avl_end((avl), AVL_RIGHT))

/*
 * From "Bit twiddling hacks", returns v < 0 ? -1 : (v > 0 ? 1 : 0)
 */
#define avl_sign(v)				\
	({					\
		typeof(v) _v = (v);		\
		((_v) > 0) - ((_v) < 0);	\
	})

/*
 * Variation on the same theme.
 */
#define avl_cmp_sign(l, r)			\
	({					\
		typeof(l) _l = (l);		\
		typeof(r) _r = (r);		\
		(_l > _r) - (_l < _r);		\
	})

#ifdef AVL_PSHARED

static inline struct avlh *
avlh_link(const struct avl *const avl,
	  const struct avlh *const holder, unsigned int dir)
{
	return (void *)avl + holder->link[avl_type2index(dir)].offset;
}

static inline void
avlh_set_link(struct avl *const avl, struct avlh *lhs, int dir, struct avlh *rhs)
{
	lhs->link[avl_type2index(dir)].offset = (void *)rhs - (void *)avl;
}

static inline struct avlh *avl_end(const struct avl *const avl, int dir)
{
	return (void *)avl + avl->end[avl_type2index(dir)].offset;
}

static inline void
avl_set_end(struct avl *const avl, int dir, struct avlh *holder)
{
	avl->end[avl_type2index(dir)].offset = (void *)holder - (void *)avl;
}

#else  /* !PSHARED */

#define avlh_link(avl, holder, dir) ((holder)->link[avl_type2index(dir)].ptr)

#define avl_end(avl, dir) ((avl)->end[avl_type2index(dir)].ptr)

static inline void
avlh_set_link(struct avl *const avl, struct avlh *lhs, int dir, struct avlh *rhs)
{
	avlh_link(avl, lhs, dir) = rhs;
}

static inline void
avl_set_end(struct avl *const avl, int dir, struct avlh *holder)
{
	avl_end(avl, dir) = holder;
}

#endif	/* !PSHARED */

static inline struct avlh *
__avl_search_inner(const struct avl *const avl, const struct avlh *n, int *delta)
{
	return avl_searchfn(avl)(avl, n, delta, 0);
}

static inline struct avlh *avl_gettop(const struct avl *const avl)
{
	return avl_top(avl);
}

static inline struct avlh *avl_gethead(const struct avl *const avl)
{
	return avl_head(avl);
}

static inline struct avlh *avl_gettail(const struct avl *const avl)
{
	return avl_tail(avl);
}

static inline unsigned int avl_getcount(const struct avl *const avl)
{
	return avl_count(avl);
}

static inline struct avlh *
avl_inorder(const struct avl *const avl,
	    struct avlh *holder,
	    const int dir)
{
	/* Assume dir == AVL_RIGHT in comments. */
	struct avlh *next;

	/*
	 * If the current node is not right threaded, then go down left,
	 * starting from its right child.
	 */
	if (avlh_has_child(avl, holder, dir)) {
		const int opp_dir = avl_opposite(dir);
		holder = avlh_link(avl, holder, dir);
		while ((next = avlh_child(avl, holder, opp_dir)))
			holder = next;
		next = holder;
	} else {
		for(;;) {
			next = avlh_up(avl, holder);
			if (next == avl_anchor(avl))
				return NULL;
			if (holder->type != dir)
				break;
			holder = next;
		}
	}

	return next;
}

static inline struct avlh *
avl_postorder(const struct avl *const avl,
	      struct avlh *const holder, const int dir)
{
	/* Assume dir == AVL_RIGHT in comments. */
	struct avlh *next = avlh_up(avl, holder);

	if (holder->type != dir)
		/*
		 * If the current node is not a right node, follow the nodes in
		 * inorder until we find a right threaded node.
		 */
		while (avlh_has_child(avl, next, dir))
			next = avl_inorder(avl, next, dir);
	else
		/*
		 * else the current node is a right node, its parent is the
		 * next in postorder.
		 */
		if (next == avl_anchor(avl))
			next = NULL;

	return next;
}

static inline struct avlh *
avl_preorder(const struct avl *const avl,
	     struct avlh *holder, const int dir)
{

	struct avlh *next;
	/* Assume dir == AVL_RIGHT in comments. */
	/*
	 * If the current node has a left child (hence is not left threaded),
	 * then return it.
	 */

	if (avlh_has_child(avl, holder, avl_opposite(dir)))
		return avlh_link(avl, holder, avl_opposite(dir));

	/*
	 * Else follow the right threads until we find a node which is not right
	 * threaded (hence has a right child) and return its right child.
	 */
	next = holder;

	while (!avlh_has_child(avl, next, dir)) {
		next = avl_inorder(avl, next, dir);
		if (next == NULL)
			return NULL;
	}

	return avlh_link(avl, next, dir);
}

static inline struct avlh *
avl_next(const struct avl *const avl, struct avlh *const holder)
{
	return avl_inorder(avl, holder, AVL_RIGHT);
}

static inline struct avlh *
avl_prev(const struct avl *const avl, struct avlh *const holder)
{
	return avl_inorder(avl, holder, AVL_LEFT);
}

static inline struct avlh *
avl_postorder_next(const struct avl *const avl, struct avlh *const holder)
{
	return avl_postorder(avl, holder, AVL_RIGHT);
}

static inline struct avlh *
avl_postorder_prev(const struct avl *const avl, struct avlh *const holder)
{
	return avl_postorder(avl, holder, AVL_LEFT);
}

static inline struct avlh *
avl_preorder_next(const struct avl *const avl, struct avlh *const holder)
{
	return avl_preorder(avl, holder, AVL_RIGHT);
}

static inline struct avlh *
avl_preorder_prev(const struct avl *const avl, struct avlh *const holder)
{
	return avl_preorder(avl, holder, AVL_LEFT);
}

static inline void avlh_init(struct avlh *const holder)
{
	holder->balance = 0;
	holder->type = 0;
}

static inline struct avlh *
avl_search(const struct avl *const avl, const struct avlh *node)
{
	struct avlh *holder;
	int delta;

	holder = __avl_search_inner(avl, node, &delta);
	if (!delta)
		return holder;

	return NULL;
}

static inline struct avlh *
avl_search_nearest(const struct avl *const avl, const struct avlh *node, int dir)
{
	struct avlh *holder;
	int delta;

	holder = __avl_search_inner(avl, node, &delta);
	if (!holder || delta != dir)
		return holder;

	return avl_inorder(avl, holder, dir);
}

static inline struct avlh *
avl_search_le(const struct avl *const avl, const struct avlh *node)
{
	return avl_search_nearest(avl, node, AVL_LEFT);
}

static inline struct avlh *
avl_search_ge(const struct avl *const avl, const struct avlh *node)
{
	return avl_search_nearest(avl, node, AVL_RIGHT);
}

int avl_insert_front(struct avl *avl, struct avlh *holder);

int avl_insert_back(struct avl *avl, struct avlh *holder);

static inline struct avlh *
avl_search_multi(const struct avl *const avl, const struct avlh *node, int dir)
{
	struct avlh *holder;
	int delta;

	holder = avl_searchfn(avl)(avl, node, &delta, dir);
	if (!delta)
		return holder;

	if (!holder)
		return NULL;

	return avl_inorder(avl, holder, -dir);
}

static inline struct avlh *
avl_search_first(const struct avl *const avl, const struct avlh *node)
{
	return avl_search_multi(avl, node, AVL_LEFT);
}

static inline struct avlh *
avl_search_last(const struct avl *const avl, const struct avlh *node)
{
	return avl_search_multi(avl, node, AVL_RIGHT);
}

/*
 * Search a node, return its parent if it could not be found.
 */
#define DECLARE_AVL_SEARCH(avl_search_inner, cmp)			\
	struct avlh *avl_search_inner(const struct avl *const avl,	\
				const struct avlh *const node,		\
				int *const pdelta, int dir)		\
	{								\
		int delta = AVL_RIGHT;					\
		struct avlh *holder = avl_top(avl), *next;		\
									\
		if (holder == NULL)					\
			goto done;					\
									\
		for (;;) {						\
			delta = cmp(node, holder);			\
			/*						\
			 * Handle duplicates keys here, according to	\
			 * "dir", if dir is:				\
			 * - AVL_LEFT, the leftmost node is returned,	\
			 * - AVL_RIGHT, the rightmost node is returned,	\
			 * - 0, the first match is returned.		\
			 */						\
			if (!(delta ?: dir))				\
				break;					\
			next = avlh_child(avl, holder, delta ?: dir);	\
			if (next == NULL)				\
				break;					\
			holder = next;					\
		}							\
									\
	  done:								\
		*pdelta = delta;					\
		return holder;						\
	}

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void avl_init(struct avl *const avl,
	      avl_search_t *searchfn, avlh_cmp_t *cmp);
  
void avl_destroy(struct avl *const avl);

int avl_insert(struct avl *const avl, struct avlh *const holder);
	
int avl_insert_front(struct avl *avl, struct avlh *holder);

int avl_insert_back(struct avl *avl, struct avlh *holder);

int avl_insert_at(struct avl *const avl,
		  struct avlh *parent, int dir, struct avlh *child);

int avl_prepend(struct avl *const avl, struct avlh *const holder);

int avl_append(struct avl *const avl, struct avlh *const holder);
	
int avl_delete(struct avl *const avl, struct avlh *node);

int avl_replace(struct avl *avl, struct avlh *oldh,
		struct avlh *newh);

struct avlh *avl_update(struct avl *const avl,
			struct avlh *const holder);

struct avlh *avl_set(struct avl *const avl,
		     struct avlh *const holder);

void avl_clear(struct avl *const avl, void (*destruct)(struct avlh *));

int avl_check(const struct avl *avl);
	
void avl_dump(FILE *file, const struct avl *const avl,
	      avlh_prn_t *prn, unsigned int indent, unsigned int len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_BOILERPLATE_AVL_H */
