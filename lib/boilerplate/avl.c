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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <boilerplate/avl.h>

/* Internal functions used for rebalancing (for insertion and deletion). */
static inline struct avlh *avlh_rotate(struct avlh *const holder, const int dir)
{
	const int opp_dir = avl_opposite(dir);
	struct avlh *const nexttop = avlh_link(holder, opp_dir);

	if (!avlh_thr_tst(nexttop, dir)) {
		struct avlh *const subtree = avlh_link(nexttop, dir);

		avlh_link(holder, opp_dir) = subtree;
		avlh_thr_clr(holder, opp_dir);
		avlh_up(subtree) = holder;
		subtree->type = opp_dir;
	} else
		avlh_thr_set(holder, opp_dir);

	avlh_link(nexttop, dir) = holder;
	avlh_thr_clr(nexttop, dir);

	avlh_up(nexttop) = avlh_up(holder);
	nexttop->type = holder->type;
	avlh_up(holder) = nexttop;
	holder->type = dir;

	avlh_parent_link(nexttop) = nexttop;

	return nexttop;
}

static inline struct avlh *avlh_dbl_rotate(struct avlh *const holder, const int dir)
{
	const int opp = avl_opposite(dir);

	avlh_rotate(avlh_link(holder, opp), opp);
	return avlh_rotate(holder, dir);
}

static struct avlh *avlh_rebalance(struct avlh *holder, const int delta)
{

	int dir = avl_sign2type(delta);
	struct avlh *const heavy_side = avlh_link(holder, dir);

	if (heavy_side->balance == -delta) {
		/* heavy_side->balance == -delta, double rotation needed. */
		holder = avlh_dbl_rotate(holder, avl_opposite(dir));

		/* recompute balances, there are three nodes involved, two of which
		   balances become null.*/
		dir = holder->balance ? avl_sign2type(holder->balance) : AVL_RIGHT;
		avlh_link(holder, dir)->balance = 0;
		avlh_link(holder, avl_opposite(dir))->balance = -holder->balance;
		holder->balance = 0;
	} else {
		/* heavy_side->balance == delta or 0, simple rotation needed.
		   the case 0 occurs only when deleting, never when inserting. */
		/* heavy_side becomes the new root. */
		avlh_rotate(holder, avl_opposite(dir));

		/* recompute balances. */
		holder->balance -= heavy_side->balance;
		heavy_side->balance -= delta;

		holder = heavy_side;
	}
	return holder;
}

/* The avlh_rebalance functions was split in two parts to allow inlining in
   the simplest case. */
static inline struct avlh *avlh_balance_add(struct avlh *const holder, const int delta)
{
	if (holder->balance == delta)
		/* we need to rebalance the current subtree. */
		return avlh_rebalance(holder, delta);

	/* the current subtree does not need rebalancing */
	holder->balance += delta;
	return holder;
}

static inline void avlh_link_child(struct avlh *const oldh,
				   struct avlh *const newh, const int side)
{
	struct avlh *const child = avlh_link(oldh, side);

	avlh_link(newh, side) = child;
	if (!avlh_thr_tst(oldh, side)) {
		/* avl_inorder won't use its tree parameter, hence NULL is Ok. */
		struct avlh *const inorder_adj = avl_inorder(NULL, oldh, side);
		const int opp_side = avl_opposite(side);
		avlh_link(inorder_adj, opp_side) = newh;
		/* Do not change child before using avl_prev... */
		avlh_up(child) = newh;
	}
}

static inline void avlh_replace(struct avlh *const oldh, struct avlh *const newh)
{
	newh->thr = oldh->thr;
	newh->type = oldh->type;
	/* Do not update the balance, this has to be done by the caller. */

	avlh_up(newh) = avlh_up(oldh);
	avlh_parent_link(oldh) = newh;

	avlh_link_child(oldh, newh, AVL_LEFT);
	avlh_link_child(oldh, newh, AVL_RIGHT);
}

/* Deletion helpers. */
static void avl_delete_leaf(struct avl *const avl, struct avlh *const node)
{
	/* Node has no child at all. It disappears and its father becomes
	   threaded on the side id was. */

	struct avlh *const new_node = avlh_up(node);
	const int dir = node->type;

	/* Suppress node. */
	avlh_link(new_node, dir) = avlh_link(node, dir);
	avlh_thr_set(new_node, dir);

	if (node == avl_end(avl, dir))
		avl_end(avl, dir) = new_node;
}

static struct avlh *avl_delete_1child(struct avl *const avl,
				      struct avlh *const node, const int dir)
{
	/* Node is threaded on one side and has a child on the other
	   side. In this case, node is replaced by its child. */

	struct avlh *const new_node = avlh_link(node, dir);

	/* Change links as if new_node was suppressed before calling
	   avlh_replace. */
	avlh_link(node, dir) = avlh_link(new_node, dir);
	if (avlh_thr_tst(new_node, dir))
		avlh_thr_set(node, dir);
	avlh_replace(node, new_node);

	if (node == avl_end(avl, avl_opposite(dir)))
		avl_end(avl, avl_opposite(dir)) = new_node;
	/* new_node->balance 0, which is correct. */
	return new_node;
}

static int avl_delete_2children(struct avl *const avl, struct avlh *const node);

/* Insertion helpers. */
static inline void avlh_attach(struct avlh *const parent,
			       struct avlh *const child, const int side)
{
	avlh_link(child, side) = avlh_link(parent, side);
	avlh_link(child, avl_opposite(side)) = parent;
	avlh_up(child) = parent;
	avlh_link(parent, side) = child;
	child->type = side;
}

/**
 * Insert a node, given its parent and the side where it should be inserted.
 * Helper for all insertion functions.
 */
static inline void avl_insert_inner(struct avl *const avl, struct avlh *parent,
				    struct avlh *const node, const int side)
{
	avlh_attach(parent, node, side);
	++avl_count(avl);

	if (parent == avl_anchor(avl)) {
		goto insert_first_and_ret;      /* Get away from fast path */
	} else if (parent == avl_end(avl, side))
		avl_end(avl, side) = node;

	/* Do not touch these for first insertion. */
	avlh_thr_clr(parent, side);
	parent->balance += avl_type2sign(side);

	while (parent->balance) {
		const int delta = avl_type2sign(parent->type);
		parent = avlh_up(parent);
		if (parent == avl_anchor(avl))
			goto inc_height_and_ret; /* Get away from fast path */
		parent = avlh_balance_add(parent, delta);
	}

	return;

insert_first_and_ret:
	avl_head(avl) = avl_tail(avl) = node;
inc_height_and_ret:
	++avl_height(avl);
	return;
}

/* External functions. */

int avl_delete(struct avl *const avl, struct avlh *node)
{
	if (!--avl_count(avl)) {
		goto delete_last_and_ret;
	}

	switch(node->thr) {
	case (AVL_THR_LEFT|AVL_THR_RIGHT): /* thr is 5 */
		avl_delete_leaf(avl, node);
		break;

	case AVL_THR_LEFT: /* only AVL_LEFT bit is on, thr is 1. */
		node = avl_delete_1child(avl, node, AVL_RIGHT);
		break;

	case AVL_THR_RIGHT: /* only AVL_RIGHT bit is on, thr is 4. */
		node = avl_delete_1child(avl, node, AVL_LEFT);
		break;

	case 0:
		return avl_delete_2children(avl, node);
	}

	/* node is the first node which needs to be rebalanced.
	   The tree is rebalanced, and contrarily to what happened for insertion,
	   the rebalancing stops when a node which is NOT balanced is met. */
	while (!node->balance) {
		const int delta = -avl_type2sign(node->type);
		node = avlh_up(node);
		if (node == avl_anchor(avl))
			goto dec_height_and_ret;
		node = avlh_balance_add(node, delta);
	}

	return 0;

delete_last_and_ret:
	avl_top(avl) = avl_head(avl) = avl_tail(avl) = avl_anchor(avl);
dec_height_and_ret:
	--avl_height(avl);
	return 0;
}

static int avl_delete_2children(struct avl *const avl, struct avlh *const node)
{
	const int dir = avl_sign2type(node->balance ? node->balance : 1);
	struct avlh *const new_node = avl_inorder(avl, node, dir);
	avl_delete(avl, new_node);
	++avl_count(avl);
	avlh_replace(node, new_node);
	new_node->balance = node->balance;
	if (avl_end(avl, dir) == node)
		avl_end(avl, dir) = new_node;
	return 0;
}

int avl_prepend(struct avl *const avl, struct avlh *const holder)
{
	struct avlh *const parent = avl_head(avl);
	int type = parent == avl_anchor(avl) ? AVL_RIGHT : AVL_LEFT;

	if (parent == avl_anchor(avl) || avl_cmp(avl)(parent, holder) < 0) {
		avl_insert_inner(avl, parent, holder, avl_type2sign(type));
		return 0;
	}

	return -EINVAL;
}

int avl_insert(struct avl *const avl, struct avlh *const holder)
{
	int delta;
	struct avlh *parent;

	parent = avl_searchfn(avl)(avl, holder, &delta);
	if (delta == 0)
		return -EBUSY;

	avl_insert_inner(avl, parent, holder, avl_sign2type(delta));
	return 0;
}

int avl_append(struct avl *const avl, struct avlh *const holder)
{
	struct avlh *const parent = avl_tail(avl);

	if (parent == avl_anchor(avl) || avl_cmp(avl)(parent, holder) > 0) {
		avl_insert_inner(avl, parent, holder, avl_type2sign(AVL_RIGHT));
		return 0;
	}

	return -EINVAL;
}

struct avlh *avl_update(struct avl *const avl, struct avlh *const holder)
{
	int delta;
	struct avlh *const oldh = avl_searchfn(avl)(avl, holder, &delta);

	if (!delta) {
		avlh_replace(oldh, holder);
		holder->balance = oldh->balance;
		return oldh;
	}

	return NULL;
}

struct avlh *avl_set(struct avl *const avl, struct avlh *const holder)
{
	int delta;
	struct avlh *const oldh = avl_searchfn(avl)(avl, holder, &delta);

	if (delta) {
		avl_insert_inner(avl, oldh, holder, avl_sign2type(delta));
		return NULL;
	}

	avlh_replace(oldh, holder);
	holder->balance = oldh->balance;
	return oldh;
}

void avl_init(struct avl *avl, avl_search_t *search, avlh_cmp_t *cmp)
{
	avlh_init(avl_anchor(avl)); /* Beware of the union; this must be first. */
	avl_cmp(avl) = cmp;
	avl_height(avl) = 0;
	avl_count(avl) = 0;
	avl_searchfn(avl) = search;
	avlh_right(avl_anchor(avl)) = avl_anchor(avl);

	avl_head(avl) = avl_tail(avl) = avl_anchor(avl);
}

void avl_destroy(struct avl *avl)
{
	avl_init(avl, NULL, NULL);
}

void avl_clear(struct avl *const avl, void (*destruct)(struct avlh *))
{
	if (destruct) {
		struct avlh *next, *holder = avl_gethead(avl);

		while (holder) {
			next = avl_postorder_next(avl, holder);
			destruct(holder);
			holder = next;
		}
	}

	avl_init(avl, avl_searchfn(avl), avl_cmp(avl));
}
