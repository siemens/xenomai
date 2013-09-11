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
#ifndef _COBALT_POSIX_REGISTRY_H
#define _COBALT_POSIX_REGISTRY_H

#include <stdarg.h>
#include <linux/list.h>
#include <cobalt/kernel/lock.h>

#define COBALT_MAXNAME 64

/* A cobalt_node_t is the holder to be added to each object which needs to be put
   in the registry. */
typedef struct cobalt_node {
    unsigned magic;
    unsigned flags;             /* COBALT_NODE_PARTIAL_INIT. */
    unsigned refcount;
    struct cobalt_node *next;
    struct cobalt_node **prev;
    char name[COBALT_MAXNAME];
} cobalt_node_t;

int cobalt_reg_pkg_init(unsigned objects_count, unsigned maxfds);

void cobalt_reg_pkg_cleanup(void);

/* Get an existing node: oflags are POSIX open style flags.
   If 0 is returned and (*nodep) is NULL, then a new node should be added
   with node_add. */
int cobalt_node_get(cobalt_node_t **nodep,
		   const char *name,
		   unsigned long magic,
		   long oflags);

/* bind a node. */
int cobalt_node_add(cobalt_node_t *node, const char *name, unsigned magic);

/* Any successful call to node_get or node_add need to be paired with a call
   node_put before a node may be unlinked. */
int cobalt_node_put(cobalt_node_t *node);

/* Remove the binding of a node to its name, if the node is still referenced,
   real destruction is deferred until the last call to node_put. */
int cobalt_node_remove(cobalt_node_t **nodep, const char *name, unsigned magic);

#define COBALT_NODE_REMOVED 1

#define cobalt_node_ref_p(node) ((node)->refcount)

#define cobalt_node_removed_p(node) \
    ((node)->flags & COBALT_NODE_REMOVED && !cobalt_node_ref_p(node))

/* A cobalt_desc_t is the structure associated with a descriptor.  */
typedef struct cobalt_desc {
    cobalt_node_t *node;
    long flags;
    int fd;
} cobalt_desc_t;

int cobalt_desc_create(cobalt_desc_t **descp, cobalt_node_t *node, long flags);

int cobalt_desc_get(cobalt_desc_t **descp, int fd, unsigned magic);

void cobalt_desc_destroy(cobalt_desc_t *desc);

#define cobalt_desc_setflags(desc, fl) ((desc)->flags = (fl))

#define cobalt_desc_getflags(desc) ((desc)->flags)

#define cobalt_desc_node(desc) ((desc)->node)

#define cobalt_desc_fd(desc) ((desc)->fd)

#define COBALT_PERMS_MASK  (O_RDONLY | O_WRONLY | O_RDWR)

/*
 * Associative lists, used for association of user-space to
 * kernel-space objects.
 */
struct mm_struct;

DECLARE_EXTERN_XNLOCK(cobalt_assoc_lock);

typedef struct {
    unsigned long key;
    struct list_head link;
} cobalt_assoc_t;

typedef struct {
    unsigned long kfd;
    cobalt_assoc_t assoc;

#define assoc2ufd(laddr) \
    ((cobalt_ufd_t *)((unsigned long) (laddr) - offsetof(cobalt_ufd_t, assoc)))
} cobalt_ufd_t;

#define cobalt_assoc_key(assoc) ((assoc)->key)

void cobalt_assocq_destroy(struct list_head *q, void (*destroy)(cobalt_assoc_t *));

int cobalt_assoc_insert(struct list_head *q,
		       cobalt_assoc_t *assoc,
		       unsigned long key);

cobalt_assoc_t *cobalt_assoc_lookup(struct list_head *q,
				  unsigned long key);

cobalt_assoc_t *cobalt_assoc_remove(struct list_head *q,
				  unsigned long key);

#endif /* _COBALT_POSIX_REGISTRY_H */
