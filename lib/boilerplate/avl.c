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
#include <memory.h>
#include <boilerplate/avl.h>

static inline unsigned int avlh_thr(const struct avl *const avl, const struct avlh *h)
{
	unsigned int result = 0;

	if (avlh_link(avl, h, AVL_LEFT) == NULL)
		result |= AVL_THR_LEFT;
	if (avlh_link(avl, h, AVL_RIGHT) == NULL)
		result |= AVL_THR_RIGHT;

	return result;
}

static inline void
avlh_set_parent_link(struct avl *const avl, struct avlh *lhs, struct avlh *rhs)
{
	avlh_set_link(avl, avlh_up(avl, lhs), lhs->type, rhs);
}

static inline void
avlh_set_left(struct avl *const avl, struct avlh *lhs, struct avlh *rhs)
{
	avlh_set_link(avl, lhs, AVL_LEFT, rhs);
}

static inline void
avlh_set_up(struct avl *const avl, struct avlh *lhs, struct avlh *rhs)
{
	avlh_set_link(avl, lhs, AVL_UP, rhs);
}

static inline void
avlh_set_right(struct avl *const avl, struct avlh *lhs, struct avlh *rhs)
{
	avlh_set_link(avl, lhs, AVL_RIGHT, rhs);
}

static inline void avl_set_top(struct avl *const avl, struct avlh *holder)
{
	avlh_set_link(avl, avl_anchor(avl), AVL_RIGHT, holder);
}

static inline void avl_set_head(struct avl *const avl, struct avlh *holder)
{
	avl_set_end(avl, AVL_LEFT, holder);
}

static inline void avl_set_tail(struct avl *const avl, struct avlh *holder)
{
	avl_set_end(avl, AVL_RIGHT, holder);
}

/* Internal functions used for rebalancing (for insertion and deletion). */
static inline struct avlh *
avlh_rotate(struct avl *const avl, struct avlh *const holder, const int dir)
{
	const int opp_dir = avl_opposite(dir);
	struct avlh *const nexttop = avlh_link(avl, holder, opp_dir);
	struct avlh *const subtree = avlh_child(avl, nexttop, dir);

	if (subtree) {
		avlh_set_link(avl, holder, opp_dir, subtree);
		avlh_set_up(avl, subtree, holder);
		subtree->type = opp_dir;
	} else
		avlh_set_link(avl, holder, opp_dir, NULL);

	avlh_set_link(avl, nexttop, dir, holder);
	avlh_set_up(avl, nexttop, avlh_up(avl, holder));
	nexttop->type = holder->type;
	avlh_set_up(avl, holder, nexttop);
	holder->type = dir;

	avlh_set_parent_link(avl, nexttop, nexttop);

	return nexttop;
}

static inline struct avlh *
avlh_dbl_rotate(struct avl *const avl, struct avlh *const holder, const int dir)
{
	const int opp = avl_opposite(dir);

	avlh_rotate(avl, avlh_link(avl, holder, opp), opp);
	return avlh_rotate(avl, holder, dir);
}

static struct avlh *
avlh_rebalance(struct avl *const avl, struct avlh *holder, const int delta)
{

	int dir = delta;
	struct avlh *const heavy_side = avlh_link(avl, holder, dir);

	if (heavy_side->balance == -delta) {
		/* heavy_side->balance == -delta, double rotation needed. */
		holder = avlh_dbl_rotate(avl, holder, avl_opposite(dir));

		/*
		 * recompute balances, there are three nodes involved, two of
		 * which balances become null.
		 */
		dir = holder->balance ?: AVL_RIGHT;
		avlh_link(avl, holder, dir)->balance = 0;
		avlh_link(avl, holder, avl_opposite(dir))->balance
			= -holder->balance;
		holder->balance = 0;
	} else {
		/*
		 * heavy_side->balance == delta or 0, simple rotation needed.
		 * the case 0 occurs only when deleting, never when inserting.
		 */

		/* heavy_side becomes the new root. */
		avlh_rotate(avl, holder, avl_opposite(dir));

		/* recompute balances. */
		holder->balance -= heavy_side->balance;
		heavy_side->balance -= delta;

		holder = heavy_side;
	}
	return holder;
}

/*
 * The avlh_rebalance functions was split in two parts to allow inlining in
 * the simplest case.
 */
static inline struct avlh *
avlh_balance_add(struct avl *const avl, struct avlh *const holder, const int delta)
{
	if (holder->balance == delta)
		/* we need to rebalance the current subtree. */
		return avlh_rebalance(avl, holder, delta);

	/* the current subtree does not need rebalancing */
	holder->balance += delta;
	return holder;
}

static inline void
avlh_link_child(struct avl *const avl, struct avlh *const oldh,
		struct avlh *const newh, const int side)
{
	struct avlh *const child = avlh_link(avl, oldh, side);

	avlh_set_link(avl, newh, side, child);
	if (avlh_has_child(avl, oldh, side))
		avlh_set_up(avl, child, newh);
}

static inline void
avlh_replace(struct avl *const avl, struct avlh *const oldh, struct avlh *const newh)
{
	newh->type = oldh->type;
	/* Do not update the balance, this has to be done by the caller. */

	avlh_set_up(avl, newh, avlh_up(avl, oldh));
	avlh_set_parent_link(avl, oldh, newh);

	avlh_link_child(avl, oldh, newh, AVL_LEFT);
	avlh_link_child(avl, oldh, newh, AVL_RIGHT);
}

/*
 * Special case, when we know that replacing a node with another will not change
 * the avl, much faster than remove + add
 */
int avl_replace(struct avl *avl, struct avlh *oldh, struct avlh *newh)
{
	struct avlh *prev, *next;

	prev = avl_prev(avl, oldh);
	next = avl_next(avl, oldh);

	if ((prev && avl_cmp(avl)(newh, prev) < 0)
	    || (next && avl_cmp(avl)(newh, next) > 0))
		return -EINVAL;

	avlh_replace(avl, oldh, newh);
	if (oldh == avl_head(avl))
		avl_set_head(avl, newh);
	if (oldh == avl_tail(avl))
		avl_set_tail(avl, newh);
	newh->balance = oldh->balance;
	return 0;
}

/* Deletion helpers. */
static void avl_delete_leaf(struct avl *const avl, struct avlh *const node)
{
	/*
	 * Node has no child at all. It disappears and its father becomes
	 * threaded on the side id was.
	 */

	struct avlh *const new_node = avlh_up(avl, node);
	const int dir = node->type;

	/* Suppress node. */
	avlh_set_link(avl, new_node, dir, avlh_link(avl, node, dir));

	if (node == avl_end(avl, dir))
		avl_set_end(avl, dir, new_node);
}

static struct avlh *avl_delete_1child(struct avl *const avl,
				      struct avlh *const node, const int dir)
{
	/*
	 * Node is threaded on one side and has a child on the other
	 * side. In this case, node is replaced by its child.
	 */

	struct avlh *const new_node = avlh_link(avl, node, dir);

	/*
	 * Change links as if new_node was suppressed before calling
	 * avlh_replace.
	 */
	avlh_set_link(avl, node, dir, avlh_link(avl, new_node, dir));
	avlh_replace(avl, node, new_node);

	if (node == avl_end(avl, avl_opposite(dir)))
		avl_set_end(avl, avl_opposite(dir), new_node);
	/* new_node->balance 0, which is correct. */
	return new_node;
}

static int avl_delete_2children(struct avl *const avl, struct avlh *const node);

/* Insertion helpers. */
static inline void
avlh_attach(struct avl *const avl, struct avlh *const parent,
	    struct avlh *const child, const int side)
{
	avlh_set_left(avl, child, NULL);
	avlh_set_right(avl, child, NULL);
	avlh_set_up(avl, child, parent);
	avlh_set_link(avl, parent, side, child);
	child->type = side;
}

/*
 * Insert a node, given its parent and the side where it should be inserted.
 * Helper for all insertion functions.
 */
static inline void avl_insert_inner(struct avl *const avl, struct avlh *parent,
				    struct avlh *const node, const int side)
{
	avlh_attach(avl, parent ?: avl_anchor(avl), node, side);
	++avl_count(avl);

	if (parent == NULL)
		goto insert_first_and_ret;	/* Get away from fast path */

	if (parent == avl_end(avl, side))
		avl_set_end(avl, side, node);

	parent->balance += side;

	while (parent->balance) {
		const int delta = parent->type;
		parent = avlh_up(avl, parent);
		if (parent == avl_anchor(avl))
			goto inc_height_and_ret; /* Get away from fast path */
		parent = avlh_balance_add(avl, parent, delta);
	}

	return;

insert_first_and_ret:
	avl_set_head(avl, node);
	avl_set_tail(avl, node);
inc_height_and_ret:
	++avl_height(avl);
}

/* External functions. */
int avl_delete(struct avl *const avl, struct avlh *node)
{
	if (!--avl_count(avl)) {
		goto delete_last_and_ret;
	}

	switch(avlh_thr(avl, node)) {
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
		const int delta = -node->type;
		node = avlh_up(avl, node);
		if (node == avl_anchor(avl))
			goto dec_height_and_ret;
		node = avlh_balance_add(avl, node, delta);
	}

	return 0;

delete_last_and_ret:
	avl_set_top(avl, NULL);
	avl_set_head(avl, NULL);
	avl_set_tail(avl, NULL);
dec_height_and_ret:
	--avl_height(avl);
	return 0;
}

static int avl_delete_2children(struct avl *const avl, struct avlh *const node)
{
	const int dir = node->balance ? node->balance : 1;
	struct avlh *const new_node = avl_inorder(avl, node, dir);
	avl_delete(avl, new_node);
	++avl_count(avl);
	avlh_replace(avl, node, new_node);
	new_node->balance = node->balance;
	if (avl_end(avl, dir) == node)
		avl_set_end(avl, dir, new_node);
	return 0;
}

int avl_prepend(struct avl *const avl, struct avlh *const holder)
{
	struct avlh *const parent = avl_head(avl);
	int type = parent == NULL ? AVL_RIGHT : AVL_LEFT;

	if (parent == NULL || avl_cmp(avl)(holder, parent) < 0) {
		avl_insert_inner(avl, parent, holder, type);
		return 0;
	}

	return -EINVAL;
}

int avl_insert_at(struct avl *const avl,
		  struct avlh *parent, int dir, struct avlh *child)
{
	if (parent == NULL)
		dir = AVL_RIGHT;
	else {
		if (!avlh_thr_tst(avl, parent, dir))
			return -EINVAL;
	}

	avl_insert_inner(avl, parent, child, dir);
	return 0;
}

int avl_insert(struct avl *const avl, struct avlh *const holder)
{
	int delta;
	struct avlh *parent;

	parent = __avl_search_inner(avl, holder, &delta);
	if (delta == 0)
		return -EBUSY;

	avl_insert_inner(avl, parent, holder, delta);

	return 0;
}

int avl_insert_front(struct avl *const avl, struct avlh *const holder)
{
	int delta;
	struct avlh *parent;

	parent = avl_searchfn(avl)(avl, holder, &delta, AVL_LEFT);

	avl_insert_inner(avl, parent, holder, delta ?: AVL_LEFT);
	return 0;
}

int avl_insert_back(struct avl *const avl, struct avlh *const holder)
{
	int delta;
	struct avlh *parent;

	parent = avl_searchfn(avl)(avl, holder, &delta, AVL_RIGHT);

	avl_insert_inner(avl, parent, holder, delta ?: AVL_RIGHT);
	return 0;
}

int avl_append(struct avl *const avl, struct avlh *const holder)
{
	struct avlh *const parent = avl_tail(avl);

	if (parent == NULL || avl_cmp(avl)(holder, parent) > 0) {
		avl_insert_inner(avl, parent, holder, AVL_RIGHT);
		return 0;
	}

	return -EINVAL;
}

struct avlh *avl_update(struct avl *const avl, struct avlh *const holder)
{
	int delta;
	struct avlh *const oldh = __avl_search_inner(avl, holder, &delta);

	if (!delta) {
		avl_replace(avl, oldh, holder);
		return oldh;
	}

	return NULL;
}

struct avlh *avl_set(struct avl *const avl, struct avlh *const holder)
{
	int delta;
	struct avlh *const oldh = __avl_search_inner(avl, holder, &delta);

	if (delta) {
		avl_insert_inner(avl, oldh, holder, delta);
		return NULL;
	}

	avl_replace(avl, oldh, holder);
	return oldh;
}

void avl_init(struct avl *const avl, avl_search_t *searchfn, avlh_cmp_t *cmp)
{
	avlh_init(avl_anchor(avl)); /* this must be first. */
	avl_cmp(avl) = cmp;
	avl_height(avl) = 0;
	avl_count(avl) = 0;
	avl_searchfn(avl) = searchfn;
	avl_set_top(avl, NULL);

	avl_set_head(avl, NULL);
	avl_set_tail(avl, NULL);
}

void avl_destroy(struct avl *const avl)
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

	avl_init(avl, NULL, NULL);
}

static inline
void avl_dumper_visit(FILE *file, const struct avl *const avl,
		      struct avlh *node,
		      avlh_prn_t *prn, const char *blank,
		      unsigned int blank_sz, char *buf,
		      unsigned int indent, unsigned int len)
{
	char bal;

	if (avlh_has_child(avl, node, AVL_RIGHT)) {
		if (blank_sz >= (unsigned int) (buf-blank)) {
			snprintf(buf, len + 3, "%*s\n", (int)len + 1, "bug!");
			fputs(buf - blank_sz, file);
		} else
			avl_dumper_visit(file, avl,
					 avlh_right(avl, node),
					 prn, blank,
					 blank_sz + indent, buf, indent, len);
	}

	switch(node->balance) {
	case 0:
		bal = '.';
		break;
	case -1:
		bal = '-';
		break;
	case 1:
		bal = '+';
		break;
	default:
		bal = '?'; /* Bug. */
	}

	(*prn)(buf, len+1, node);
	buf[len] = bal;
	buf[len+1] = '\n';
	buf[len+2] = '\0';

	fputs(buf - blank_sz, file);

	if (avlh_has_child(avl, node, AVL_LEFT)) {
		if (blank_sz >= (unsigned int)(buf - blank)) {
			snprintf(buf, len + 3, "%*s\n", (int)len + 1, "bug!");
			fputs(buf-blank_sz, file);
		} else
			avl_dumper_visit(file, avl,
					 avlh_left(avl, node),
					 prn, blank,
					 blank_sz + indent, buf, indent, len);
	}
}

void avl_dump(FILE *file, const struct avl *const avl,
	      avlh_prn_t *prn, unsigned int indent, unsigned int len)
{

	struct avlh *holder = avl_gettop(avl);

	putc('\n', file);
	if (!holder)
		fputs("Empty.\n", file);
	else {
		size_t blank_sz = (avl_height(avl) - 1) * indent;
		char buffer[blank_sz + len + 3];
		/* 3 == balance char + sizeof("\n\0") */
		memset(buffer, ' ', blank_sz);

		avl_dumper_visit(file, avl, holder, prn, buffer, 0,
				 buffer + blank_sz, indent, len);
	}
	fflush(file);
}

static int avl_check_visit(const struct avl *avl,
			   struct avlh *node, unsigned int level)
{
	int err;

	if (!avlh_has_child(avl, node, AVL_RIGHT))
		goto check_balance;

	if (level > avl_height(avl)) {
		fprintf(stderr, "too much recursion\n");
		return -EINVAL;
	}

	err = avl_check_visit(avl, avlh_right(avl, node), level + 1);
	if (err < 0)
		return err;

check_balance:
	switch(node->balance) {
	case 0:
		break;
	case -1:
		break;
	case 1:
		break;
	default:
		fprintf(stderr, "invalid balance\n");
		return -EINVAL;
	}

	if (!avlh_has_child(avl, node, AVL_LEFT))
		return 0;

	err = avl_check_visit(avl, avlh_left(avl, node), level + 1);
	if (err < 0)
		return err;

	return 0;
}

int avl_check(const struct avl *avl)
{
	struct avlh *holder = avl_gettop(avl), *last;
	int err;

	if (!holder)
		return 0;

	err = avl_check_visit(avl, holder, 0);
	if (err < 0)
		return err;

	last = NULL;
	for (holder = avl_gethead(avl); holder; holder = avl_next(avl, holder)) {
		if (last != NULL)
			if (avl_cmp(avl)(holder, last) < 0) {
				fprintf(stderr, "disordered nodes\n");
				return -EINVAL;
			}
		last = holder;
	}

	return 0;
}
