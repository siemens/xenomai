#ifndef PSE51_REGISTRY_H
#define PSE51_REGISTRY_H

#include <stdarg.h>
#include <nucleus/synch.h>
#include <posix/posix.h>

#define PSE51_MAXNAME 64

/* A pse51_node_t is the holder to be added to each object which needs to be put
   in the registry. */
typedef struct pse51_node {
    unsigned magic;
    unsigned flags;             /* PSE51_NODE_PARTIAL_INIT. */
    unsigned refcount;
    xnsynch_t *completion_synch;
    /* pse51_unlink_t *dest_hook; */

    struct pse51_node *next;
    struct pse51_node **prev;
    char name[PSE51_MAXNAME];
} pse51_node_t;

int pse51_reg_pkg_init(unsigned objects_count, unsigned maxfds);

void pse51_reg_pkg_cleanup(void);

/* Get an existing node: oflags are POSIX open style flags.
   If 0 is returned and (*nodep) is NULL, then a new node should be added
   with node_add*. */
int pse51_node_get(pse51_node_t **nodep,
                   const char *name,
                   unsigned long magic,
                   long oflags);

/* bind a node : use node_add for simple objects (eg. sem_open), node_add_start
   and node_add_finished for objects which need to be initialized outside of any
   critical sections (eg. mq_open). */
int pse51_node_add(pse51_node_t *node, const char *name, unsigned magic);

int pse51_node_add_start(pse51_node_t *node,
                         const char *name,
                         unsigned magic,
                         xnsynch_t *competion_synch);

void pse51_node_add_finished(pse51_node_t *node, int error);

/* Any successful call to node_get or node_add need to be paired with a call
   node_put before a node may be unlinked. */
int pse51_node_put(pse51_node_t *node);

/* Remove the binding of a node to its name, if the node is still referenced,
   real destruction is deferred until the last call to node_put. */
int pse51_node_remove(pse51_node_t **nodep, const char *name, unsigned magic);

#define PSE51_NODE_REMOVED 2

#define pse51_node_ref_p(node) ((node)->refcount)

#define pse51_node_removed_p(node) \
    ((node)->flags & PSE51_NODE_REMOVED && !pse51_node_ref_p(node))

/* A pse51_desc_t is the structure associated with a descriptor.  */
typedef struct pse51_desc {
    pse51_node_t *node;
    long flags;
    int fd;
} pse51_desc_t;

int pse51_desc_create(pse51_desc_t **descp, pse51_node_t *node);

int pse51_desc_get(pse51_desc_t **descp, int fd, unsigned magic);

int pse51_desc_destroy(pse51_desc_t *desc);

#define pse51_desc_setflags(desc, fl) ((desc)->flags = fl)

#define pse51_desc_getflags(desc) ((desc)->flags)

#define pse51_desc_node(desc) ((desc)->node)

#define pse51_desc_fd(desc) ((desc)->fd)

#define PSE51_PERMS_MASK  (O_RDONLY | O_WRONLY | O_RDWR)

#endif /* PSE51_REGISTRY_H */
