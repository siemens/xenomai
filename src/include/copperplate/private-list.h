/*
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _COPPERPLATE_PRIVATE_LIST_H
#define _COPPERPLATE_PRIVATE_LIST_H

#ifndef _COPPERPLATE_LIST_H
#error "Do not include this file directly. Use <copperplate/list.h> instead."
#endif

struct pvholder {
	struct pvholder *next;
	struct pvholder *prev;
};

struct pvlist {
	struct pvholder head;
};

#define DEFINE_PRIVATE_LIST(name) \
	struct pvlist name = { .head = { .next = &(name.head), .prev = &(name.head) } }

#define INIT_PRIVATE_HOLDER(name) \
	{ .next = &(name), .prev = &(name) }

static inline void initpvh(struct pvholder *holder)
{
	holder->next = holder;
	holder->prev = holder;
}

static inline void atpvh(struct pvholder *head, struct pvholder *holder)
{
	/* Inserts the new element right after the heading one. */
	holder->prev = head;
	holder->next = head->next;
	holder->next->prev = holder;
	head->next = holder;
}

static inline void dtpvh(struct pvholder *holder)
{
	holder->prev->next = holder->next;
	holder->next->prev = holder->prev;
}

static inline void pvlist_init(struct pvlist *list)
{
	initpvh(&list->head);
}

static inline void pvholder_init(struct pvholder *holder)
{
	initpvh(holder);
}

/*
 * XXX: pvholder_init() is mandatory if you later want to use this
 * predicate.
 */
static inline int pvholder_linked(struct pvholder *holder)
{
	return !(holder->prev == holder->next &&
		 holder->prev == holder);
}

static inline void pvlist_prepend(struct pvholder *holder, struct pvlist *list)
{
	atpvh(&list->head, holder);
}

static inline void pvlist_append(struct pvholder *holder, struct pvlist *list)
{
	atpvh(list->head.prev, holder);
}

static inline void pvlist_insert(struct pvholder *next, struct pvholder *prev)
{
	atpvh(prev, next);
}

static inline void pvlist_join(struct pvlist *lsrc, struct pvlist *ldst)
{
	struct pvholder *headsrc = lsrc->head.next;
	struct pvholder *tailsrc = lsrc->head.prev;
	struct pvholder *headdst = &ldst->head;

	headsrc->prev->next = tailsrc->next;
	tailsrc->next->prev = headsrc->prev;
	headsrc->prev = headdst;
	tailsrc->next = headdst->next;
	headdst->next->prev = tailsrc;
	headdst->next = headsrc;
}

static inline void pvlist_remove(struct pvholder *holder)
{
	dtpvh(holder);
}

static inline void pvlist_remove_init(struct pvholder *holder)
{
	dtpvh(holder);
	initpvh(holder);
}

static inline int pvlist_empty(const struct pvlist *list)
{
	return list->head.next == &list->head;
}

static inline struct pvholder *pvlist_pop(struct pvlist *list)
{
	struct pvholder *holder = list->head.next;
	pvlist_remove_init(holder);
	return holder;
}

static inline int pvlist_heading_p(const struct pvholder *holder,
				   const struct pvlist *list)
{
	return list->head.next == holder;
}

#define pvlist_entry(ptr, type, member)				\
	container_of(ptr, type, member)

#define pvlist_first_entry(list, type, member)			\
	pvlist_entry((list)->head.next, type, member)

#define pvlist_last_entry(list, type, member)			\
	pvlist_entry((list)->head.prev, type, member)

#define pvlist_pop_entry(list, type, member) ({				\
			struct pvholder *__holder = pvlist_pop(list);	\
			pvlist_entry(__holder, type, member); })

#define pvlist_for_each(pos, list)					\
	for (pos = (list)->head.next;					\
	     pos != &(list)->head; pos = (pos)->next)

#define pvlist_for_each_reverse(pos, list)				\
	for (pos = (list)->head.prev;					\
	     pos != &(list)->head; pos = (pos)->prev)

#define pvlist_for_each_safe(pos, tmp, list)				\
	for (pos = (list)->head.next,					\
		     tmp = (pos)->next;					\
	     pos != &(list)->head;					\
	     pos = tmp, tmp = (pos)->next)

#define pvlist_for_each_entry(pos, list, member)			\
	for (pos = pvlist_entry((list)->head.next,			\
			      typeof(*pos), member);			\
	     &(pos)->member != &(list)->head;				\
	     pos = pvlist_entry((pos)->member.next,			\
			      typeof(*pos), member))

#define pvlist_for_each_entry_safe(pos, tmp, list, member)		\
	for (pos = pvlist_entry((list)->head.next,			\
			      typeof(*pos), member),			\
		     tmp = pvlist_entry((pos)->member.next,		\
				      typeof(*pos), member);		\
	     &(pos)->member != &(list)->head;				\
	     pos = tmp, tmp = pvlist_entry((pos)->member.next,		\
					 typeof(*pos), member))

#define pvlist_for_each_entry_reverse(pos, list, member)		\
	for (pos = pvlist_entry((list)->head.prev,			\
			      typeof(*pos), member);			\
	     &pos->member != &(list)->head;				\
	     pos = pvlist_entry(pos->member.prev,			\
			      typeof(*pos), member))

#endif /* !_COPPERPLATE_PRIVATE_LIST_H */
