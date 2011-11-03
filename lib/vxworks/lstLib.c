/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <vxworks/errnoLib.h>
#include <vxworks/lstLib.h>

void lstExtract(LIST *lsrc, NODE *nstart, NODE *nend, LIST *ldst)
{
	struct pvholder *nholder = &nstart->link, *holder;
	struct NODE *n;
	int nitems = 0;

	do {	/* XXX: terribly inefficient, but plain simple... */
		holder = nholder;
		nholder = holder->next;
		pvlist_remove_init(holder);
		pvlist_append(holder, &ldst->list);
		n = container_of(holder, struct NODE, link);
		n->list = ldst;
		nitems++;
	} while (holder != &nend->link);

	lsrc->count -= nitems;
	ldst->count += nitems;
}

NODE *lstNth(LIST *l, int nodenum)
{
	struct pvholder *holder;
	int nth;

	if (l == NULL || nodenum <= 0 || pvlist_empty(&l->list))
		return NULL;

	if (nodenum <= l->count >> 2) { /* nodenum is 1-based. */
		nth = 1;
		pvlist_for_each(holder, &l->list)
			if (nodenum == nth++)
				return container_of(holder, struct NODE, link);
	} else {
		nth = l->count;
		pvlist_for_each_reverse(holder, &l->list)
			if (nodenum == nth--)
				return container_of(holder, struct NODE, link);
	}

	return NULL;
}

NODE *lstNStep(NODE *n, int steps)
{
	struct pvholder *holder = &n->link;

	if (steps == 0)
		return n;

	if (steps < 0) {
		do
			holder = holder->prev;
		while (holder->prev != &n->link && ++steps < 0);
	} else {
		do
			holder = holder->next;
		while (holder->next != &n->link && --steps > 0);
	}

	if (steps != 0)
		return NULL;

	return container_of(holder, struct NODE, link);
}

int lstFind(LIST *l, NODE *n)
{
	struct pvholder *holder;
	int nth = 1;

	if (l == NULL || pvlist_empty(&l->list))
		return ERROR;

	pvlist_for_each(holder, &l->list) {
		if (holder == &n->link)
			return nth;
		nth++;
	}

	return ERROR;
}

void lstConcat(LIST *ldst, LIST *lsrc)
{
	struct pvholder *holder;
	struct NODE *n;

	if (pvlist_empty(&lsrc->list))
		return;

	pvlist_for_each(holder, &lsrc->list) {
		n = container_of(holder, struct NODE, link);
		n->list = ldst;
	}

	pvlist_join(&lsrc->list, &ldst->list);
	ldst->count += lsrc->count;
	lsrc->count = 0;
}
