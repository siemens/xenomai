#ifndef COBALT_REGISTRY_H
#define COBALT_REGISTRY_H

#include <stdarg.h>
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

int cobalt_desc_destroy(cobalt_desc_t *desc);

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

typedef struct list_head cobalt_assocq_t;

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

#define cobalt_assocq_init(q) (INIT_LIST_HEAD(q))

#define cobalt_assoc_key(assoc) ((assoc)->key)

void cobalt_assocq_destroy(cobalt_assocq_t *q, void (*destroy)(cobalt_assoc_t *));

int cobalt_assoc_insert(cobalt_assocq_t *q,
		       cobalt_assoc_t *assoc,
		       unsigned long key);

cobalt_assoc_t *cobalt_assoc_lookup(cobalt_assocq_t *q,
				  unsigned long key);

cobalt_assoc_t *cobalt_assoc_remove(cobalt_assocq_t *q,
				  unsigned long key);

#endif /* COBALT_REGISTRY_H */
