/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/fcntl.h>
#include "internal.h"
#include "thread.h"

#define BITS_PER_INT 32

struct {
	cobalt_node_t **node_buckets;
	unsigned buckets_count;

	cobalt_desc_t **descs;
	unsigned maxfds;
	unsigned *fdsmap;
	unsigned mapsz;
} cobalt_reg;

static unsigned cobalt_reg_crunch(const char *key)
{
	unsigned h = 0, g;

#define HQON    24		/* Higher byte position */
#define HBYTE   0xf0000000	/* Higher nibble on */

	while (*key) {
		h = (h << 4) + *key++;
		if ((g = (h & HBYTE)) != 0)
			h = (h ^ (g >> HQON)) ^ g;
	}

	return h % cobalt_reg.buckets_count;
}

static int cobalt_node_lookup(cobalt_node_t *** node_linkp,
			     const char *name, unsigned long magic)
{
	cobalt_node_t **node_link;

	if (strnlen(name, sizeof((*node_link)->name)) ==
	    sizeof((*node_link)->name))
		return ENAMETOOLONG;

	node_link = &cobalt_reg.node_buckets[cobalt_reg_crunch(name)];

	while (*node_link) {
		cobalt_node_t *node = *node_link;

		if (!strncmp(node->name, name, COBALT_MAXNAME)
		    && node->magic == magic)
			break;

		node_link = &node->next;
	}

	*node_linkp = node_link;
	return 0;
}

static void cobalt_node_unbind(cobalt_node_t * node)
{
	cobalt_node_t **node_link;

	node_link = node->prev;
	*node_link = node->next;
	if (node->next)
		node->next->prev = node_link;
	node->prev = NULL;
	node->next = NULL;
}

int cobalt_node_add(cobalt_node_t * node, const char *name, unsigned magic)
{
	cobalt_node_t **node_link;
	int err;

	err = cobalt_node_lookup(&node_link, name, magic);

	if (err)
		return err;

	if (*node_link)
		return EEXIST;

	node->magic = magic;
	node->flags = 0;
	node->refcount = 1;

	/* Insertion in hash table. */
	node->next = NULL;
	node->prev = node_link;
	*node_link = node;
	strcpy(node->name, name);	/* name length is checked in
					   cobalt_node_lookup. */

	return 0;
}

int cobalt_node_put(cobalt_node_t * node)
{
	if (!cobalt_node_ref_p(node))
		return EINVAL;

	--node->refcount;
	return 0;
}

int cobalt_node_remove(cobalt_node_t ** nodep, const char *name, unsigned magic)
{
	cobalt_node_t *node, **node_link;
	int err;

	err = cobalt_node_lookup(&node_link, name, magic);

	if (err)
		return err;

	node = *node_link;

	if (!node)
		return ENOENT;

	*nodep = node;
	node->magic = ~node->magic;
	node->flags |= COBALT_NODE_REMOVED;
	cobalt_node_unbind(node);
	return 0;
}

/* Look for a node and check the POSIX open flags. */
int cobalt_node_get(cobalt_node_t ** nodep,
		   const char *name, unsigned long magic, long oflags)
{
	cobalt_node_t *node, **node_link;
	int err;

	err = cobalt_node_lookup(&node_link, name, magic);
	if (err)
		return err;

	node = *node_link;
	if (node && (oflags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
		return EEXIST;

	if (!node && !(oflags & O_CREAT))
		return ENOENT;

	*nodep = node;
	if (!node)
		return 0;

	++node->refcount;

	return 0;
}

static int cobalt_reg_fd_get(void)
{
	unsigned i;

	for (i = 0; i < cobalt_reg.mapsz; i++)
		if (cobalt_reg.fdsmap[i]) {
			int fd = ffnz(cobalt_reg.fdsmap[i]);

			cobalt_reg.fdsmap[i] &= ~(1 << fd);
			return fd + BITS_PER_INT * i;
		}

	return -1;
}

static void cobalt_reg_fd_put(int fd)
{
	unsigned i, bit;

	i = fd / BITS_PER_INT;
	bit = 1 << (fd % BITS_PER_INT);

	cobalt_reg.fdsmap[i] |= bit;
	cobalt_reg.descs[fd] = NULL;
}

static int cobalt_reg_fd_lookup(cobalt_desc_t ** descp, int fd)
{
	unsigned i, bit;

	if (fd > cobalt_reg.maxfds)
		return EBADF;

	i = fd / BITS_PER_INT;
	bit = 1 << (fd % BITS_PER_INT);

	if ((cobalt_reg.fdsmap[i] & bit))
		return EBADF;

	*descp = cobalt_reg.descs[fd];
	return 0;
}

int cobalt_desc_create(cobalt_desc_t ** descp, cobalt_node_t * node, long flags)
{
	cobalt_desc_t *desc;
	spl_t s;
	int fd;

	desc = xnmalloc(sizeof(*desc));
	if (desc == NULL)
		return ENOSPC;

	xnlock_get_irqsave(&nklock, s);
	fd = cobalt_reg_fd_get();
	if (fd == -1) {
		xnlock_put_irqrestore(&nklock, s);
		xnfree(desc);
		return EMFILE;
	}

	cobalt_reg.descs[fd] = desc;
	desc->node = node;
	desc->fd = fd;
	desc->flags = flags;
	xnlock_put_irqrestore(&nklock, s);

	*descp = desc;
	return 0;
}

int cobalt_desc_destroy(cobalt_desc_t * desc)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	cobalt_reg_fd_put(desc->fd);
	xnlock_put_irqrestore(&nklock, s);
	xnfree(desc);
	return 0;
}

int cobalt_desc_get(cobalt_desc_t ** descp, int fd, unsigned magic)
{
	cobalt_desc_t *desc;
	int err;

	err = cobalt_reg_fd_lookup(&desc, fd);

	if (err)
		return err;

	if (desc->node->magic != magic
	    /* In case the object has been unlinked. */
	    && desc->node->magic != ~magic)
		return EBADF;

	*descp = desc;
	return 0;
}

DEFINE_XNLOCK(cobalt_assoc_lock);

static int cobalt_assoc_lookup_inner(struct list_head *q,
				     cobalt_assoc_t **passoc,
				     unsigned long key)
{
	cobalt_assoc_t *assoc = NULL;

	if (list_empty(q))
		goto out;

	list_for_each_entry(assoc, q, link) {
		if (assoc->key == key) {
			*passoc = assoc;
			return 1;
		}
		if (assoc->key > key)
			goto out;
	}

	assoc = NULL;
out:
	*passoc = assoc;

	return 0;
}

int cobalt_assoc_insert(struct list_head *q,
			cobalt_assoc_t * assoc, unsigned long key)
{
	cobalt_assoc_t *next;
	spl_t s;

	xnlock_get_irqsave(&cobalt_assoc_lock, s);

	if (cobalt_assoc_lookup_inner(q, &next, key)) {
		xnlock_put_irqrestore(&cobalt_assoc_lock, s);
		return -EBUSY;
	}

	assoc->key = key;
	if (next)
		list_add_tail(&assoc->link, &next->link);
	else
		list_add_tail(&assoc->link, q);

	xnlock_put_irqrestore(&cobalt_assoc_lock, s);

	return 0;
}

cobalt_assoc_t *cobalt_assoc_lookup(struct list_head *q,
				    unsigned long key)
{
	cobalt_assoc_t *assoc;
	unsigned found;
	spl_t s;

	xnlock_get_irqsave(&cobalt_assoc_lock, s);
	found = cobalt_assoc_lookup_inner(q, &assoc, key);
	xnlock_put_irqrestore(&cobalt_assoc_lock, s);

	return found ? assoc : NULL;
}

cobalt_assoc_t *cobalt_assoc_remove(struct list_head *q,
				    unsigned long key)
{
	cobalt_assoc_t *assoc;
	spl_t s;

	xnlock_get_irqsave(&cobalt_assoc_lock, s);
	if (!cobalt_assoc_lookup_inner(q, &assoc, key)) {
		xnlock_put_irqrestore(&cobalt_assoc_lock, s);
		return NULL;
	}

	list_del(&assoc->link);
	xnlock_put_irqrestore(&cobalt_assoc_lock, s);

	return assoc;
}

void cobalt_assocq_destroy(struct list_head *q, void (*destroy)(cobalt_assoc_t *))
{
	cobalt_assoc_t *assoc, *tmp;
	spl_t s;

	xnlock_get_irqsave(&cobalt_assoc_lock, s);

	if (list_empty(q))
		goto out;

	list_for_each_entry_safe(assoc, tmp, q, link) {
		list_del(&assoc->link);
		xnlock_put_irqrestore(&cobalt_assoc_lock, s);
		if (destroy)
			destroy(assoc);
		xnlock_get_irqsave(&cobalt_assoc_lock, s);
	}
out:
	xnlock_put_irqrestore(&cobalt_assoc_lock, s);
}

struct cobalt_kqueues cobalt_global_kqueues;

int cobalt_reg_pkg_init(unsigned buckets_count, unsigned maxfds)
{
	size_t size, mapsize;
	char *chunk;
	unsigned i;

	mapsize = maxfds / BITS_PER_INT;
	if (maxfds % BITS_PER_INT)
		++mapsize;

	size = sizeof(cobalt_node_t) * buckets_count +
		sizeof(cobalt_desc_t) * maxfds + sizeof(unsigned) * mapsize;

	chunk = kmalloc(size, GFP_KERNEL);
	if (chunk == NULL)
		return ENOMEM;

	cobalt_reg.node_buckets = (cobalt_node_t **) chunk;
	cobalt_reg.buckets_count = buckets_count;
	for (i = 0; i < buckets_count; i++)
		cobalt_reg.node_buckets[i] = NULL;

	chunk += sizeof(cobalt_node_t) * buckets_count;
	cobalt_reg.descs = (cobalt_desc_t **) chunk;
	for (i = 0; i < maxfds; i++)
		cobalt_reg.descs[i] = NULL;

	chunk += sizeof(cobalt_desc_t) * maxfds;
	cobalt_reg.fdsmap = (unsigned *)chunk;
	cobalt_reg.maxfds = maxfds;
	cobalt_reg.mapsz = mapsize;

	/* Initialize fds map. Bit set means "descriptor free". */
	for (i = 0; i < maxfds / BITS_PER_INT; i++)
		cobalt_reg.fdsmap[i] = ~0;
	if (maxfds % BITS_PER_INT)
		cobalt_reg.fdsmap[mapsize - 1] =
		    (1 << (maxfds % BITS_PER_INT)) - 1;

	xnlock_init(&cobalt_assoc_lock);

	return 0;
}

void cobalt_reg_pkg_cleanup(void)
{
	unsigned i;

	for (i = 0; i < cobalt_reg.maxfds; i++)
		if (cobalt_reg.descs[i]) {
#if XENO_DEBUG(COBALT)
			printk(XENO_INFO "releasing descriptor %d\n", i);
#endif /* XENO_DEBUG(COBALT) */
			cobalt_desc_destroy(cobalt_reg.descs[i]);
		}
#if XENO_DEBUG(COBALT)
	for (i = 0; i < cobalt_reg.buckets_count; i++) {
		cobalt_node_t *node;
		for (node = cobalt_reg.node_buckets[i];
		     node;
		     node = node->next)
		  printk(XENO_WARN "node \"%s\" left aside\n",
			 node->name);
	}
#endif /* XENO_DEBUG(COBALT) */

	kfree(cobalt_reg.node_buckets);
}
