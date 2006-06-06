/*
 * Copyright (C) 2005 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

/**
 * @ingroup posix
 * @defgroup posix_shm Shared memory services.
 *
 * Shared memory services.
 * 
 *@{*/

#include <nucleus/heap.h>
#include <posix/registry.h>
#include <posix/internal.h>
#include <posix/thread.h>
#include <posix/shm.h>

#ifdef __KERNEL__
#include <asm/semaphore.h>
#endif /* __KERNEL__ */

typedef struct pse51_shm {
    pse51_node_t nodebase;

#define node2shm(naddr) \
    ((pse51_shm_t *) (((char *)(naddr)) - offsetof(pse51_shm_t, nodebase)))

    xnholder_t link;            /* link in shmq */

#define link2shm(laddr)                                                 \
    ((pse51_shm_t *) (((char *)(laddr)) - offsetof(pse51_shm_t, link)))

    struct semaphore maplock;
    xnheap_t heapbase;
    void *addr;
    size_t size;

#define heap2shm(haddr) \
    ((pse51_shm_t *) (((char *)(haddr)) - offsetof(pse51_shm_t, heapbase)))

    xnqueue_t mappings;

} pse51_shm_t;

typedef struct pse51_shm_map {
    void *addr;
    size_t size;

    xnholder_t link;

#define link2map(laddr) \
    ((pse51_shm_map_t *) (((char *)(laddr)) - offsetof(pse51_shm_map_t, link)))
    
} pse51_shm_map_t;

static xnqueue_t pse51_shmq;

static void pse51_shm_init(pse51_shm_t *shm)
{
    shm->addr = NULL;
    shm->size = 0;
    sema_init(&shm->maplock, 1);
    initq(&shm->mappings);

    inith(&shm->link);
    appendq(&pse51_shmq, &shm->link);
}

#ifndef CONFIG_XENO_OPT_PERVASIVE
static void pse51_free_heap_extent(xnheap_t *heap,
                                   void *extent,
                                   u_long size,
                                   void *cookie)
{
    xnarch_sysfree(extent, size);
}
#endif /* CONFIG_XENO_OPT_PERVASIVE */

/* Must be called nklock locked, irq off. */
static void pse51_shm_destroy(pse51_shm_t *shm, int force)
{
    spl_t ignored;

    removeq(&pse51_shmq, &shm->link);
    xnlock_clear_irqon(&nklock);

    down(&shm->maplock);
    
    if (shm->addr)
        {
        xnheap_free(&shm->heapbase, shm->addr);
        
#ifdef CONFIG_XENO_OPT_PERVASIVE
	xnheap_destroy_mapped(&shm->heapbase);
#else /* !CONFIG_XENO_OPT_PERVASIVE. */
        xnheap_destroy(&shm->heapbase, &pse51_free_heap_extent, NULL);
#endif /* !CONFIG_XENO_OPT_PERVASIVE. */

	shm->addr = NULL;
        shm->size = 0;
        }

    if (force)
        {
        xnholder_t *holder;

        while ((holder = getq(&shm->mappings)))
            {
            pse51_shm_map_t *mapping = link2map(holder);
            xnfree(mapping);
            }
        }

    up(&shm->maplock);
    xnlock_get_irqsave(&nklock, ignored);
}

static pse51_shm_t *pse51_shm_get(pse51_desc_t **pdesc, int fd, unsigned inc)
{
    pse51_shm_t *shm;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    shm = (pse51_shm_t *) ERR_PTR(-pse51_desc_get(pdesc, fd, PSE51_SHM_MAGIC));

    if(IS_ERR(shm))
        goto out;

    shm = node2shm(pse51_desc_node(*pdesc));

    shm->nodebase.refcount += inc;

  out:
    xnlock_put_irqrestore(&nklock, s);

    return shm;
}

static void pse51_shm_put(pse51_shm_t *shm, unsigned dec)
{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    while (dec--)
        pse51_node_put(&shm->nodebase);

    if (pse51_node_removed_p(&shm->nodebase))
        {
        pse51_shm_destroy(shm, 0);
        xnfree(shm);
        }

    xnlock_put_irqrestore(&nklock, s);
}

/**
 * Open a shared memory object.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/shm_open.html
 * 
 */
int shm_open(const char *name, int oflags, mode_t mode)
{
    pse51_node_t *node;
    pse51_desc_t *desc;
    pse51_shm_t *shm;
    int err, fd;
    spl_t s;

    /* From root context only. */
    if (xnpod_asynch_p() || !xnpod_root_p())
        {
        thread_set_errno(EPERM);
        return -1;
        }    

    xnlock_get_irqsave(&nklock, s);

    err = pse51_node_get(&node, name, PSE51_SHM_MAGIC, oflags);
    if (err)
        goto error;

    if (!node)
        {
        /* We must create the shared memory object, not yet allocated. */
        shm = (pse51_shm_t *) xnmalloc(sizeof(*shm));
        
        if (!shm)
            {
            err = ENOMEM;
            goto error;
            }

        err = pse51_node_add(&shm->nodebase, name, PSE51_SHM_MAGIC);
        if (err)
            {
            xnfree(shm);
            goto error;
            }

        pse51_shm_init(shm);
        }
    else
        shm = node2shm(node);

    err = pse51_desc_create(&desc, &shm->nodebase);
    if (err)
        goto err_shm_put;

    pse51_desc_setflags(desc, oflags & PSE51_PERMS_MASK);

    fd = pse51_desc_fd(desc);
    xnlock_put_irqrestore(&nklock, s);

    if ((oflags & O_TRUNC) && ftruncate(fd, 0))
        {
        close(fd);
        return -1;
        }
    
    return fd;

  err_shm_put:
    pse51_shm_put(shm, 1);

  error:
    xnlock_put_irqrestore(&nklock, s);
    thread_set_errno(err);
    return -1;
}

/**
 * Unlink a shared memory object.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/shm_unlink.html
 * 
 */
int shm_unlink(const char *name)
{
    pse51_node_t *node;
    pse51_shm_t *shm;
    int err;
    spl_t s;

    if (xnpod_asynch_p() || !xnpod_root_p())
        {
        err = EPERM;
        goto error;
        }    

    xnlock_get_irqsave(&nklock, s);

    err = pse51_node_remove(&node, name, PSE51_SHM_MAGIC);

    if (err)
        {
        xnlock_put_irqrestore(&nklock, s);
error:
        thread_set_errno(err);
        return -1;
        }

    shm = node2shm(node);
    pse51_shm_put(shm, 0);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

/**
 * Close a file descriptor.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/close.html
 * 
 */
int close(int fd)
{
    pse51_desc_t *desc;
    pse51_shm_t *shm;
    spl_t s;
    int err;

    if (xnpod_asynch_p() || !xnpod_root_p())
        {
        thread_set_errno(EPERM);
        return -1;
        }    

    xnlock_get_irqsave(&nklock, s);

    shm = pse51_shm_get(&desc, fd, 0);

    if(IS_ERR(shm))
        {
        err = -PTR_ERR(shm);
        goto error;
        }

    err = pse51_desc_destroy(desc);

    if(err)
        goto error;

    pse51_shm_put(shm, 1);
    xnlock_put_irqrestore(&nklock, s);
    return 0;

  error:
    xnlock_put_irqrestore(&nklock, s);
    thread_set_errno(err);
    return -1;
}

/**
 * Truncate a file or shared memory object to a specified length.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/ftruncate.html
 * 
 */
int ftruncate(int fd, off_t len)
{
    unsigned desc_flags;
    pse51_desc_t *desc;
    pse51_shm_t *shm;
    int err;
    spl_t s;

    if (xnpod_asynch_p() || !xnpod_root_p())
        {
        err = EPERM;
        goto error;
        }

    if (len < 0)
        {
        err = EINVAL;
        goto error;
        }
    
    xnlock_get_irqsave(&nklock, s);
    shm = pse51_shm_get(&desc, fd, 1);

    if(IS_ERR(shm))
        {
        xnlock_put_irqrestore(&nklock, s);
        err = -PTR_ERR(shm);
        goto error;
        }

    desc_flags = pse51_desc_getflags(desc);
    xnlock_put_irqrestore(&nklock, s);

    if (down_interruptible(&shm->maplock))
        {
        err = EINTR;
        goto err_shm_put;
        }

    /* Allocate one page more for alignment (the address returned by mmap
       is aligned). */
    if (len)
        {
        len += PAGE_SIZE + PAGE_ALIGN(xnheap_overhead(len, PAGE_SIZE));
        len = PAGE_ALIGN(len);
        }

    err = 0;
    if (!countq(&shm->mappings))
        {
        if (shm->addr)
            {
            xnheap_free(&shm->heapbase, shm->addr);
#ifdef CONFIG_XENO_OPT_PERVASIVE
            xnheap_destroy_mapped(&shm->heapbase);
#else /* !CONFIG_XENO_OPT_PERVASIVE. */
            xnheap_destroy(&shm->heapbase, &pse51_free_heap_extent, NULL);
#endif /* !CONFIG_XENO_OPT_PERVASIVE. */

            shm->addr = NULL;
            shm->size = 0;
            }

        if (len)
            {
#ifdef CONFIG_XENO_OPT_PERVASIVE
            int flags = len <= 128*1024 ? GFP_USER : 0;
            err = -xnheap_init_mapped(&shm->heapbase, len, flags);
#else /* !CONFIG_XENO_OPT_PERVASIVE. */
            {
            void *heapaddr = xnarch_sysalloc(len);

            if (heapaddr)
                err = -xnheap_init(&shm->heapbase, heapaddr, len, PAGE_SIZE);
            else
                err = ENOMEM;
            }
#endif /* !CONFIG_XENO_OPT_PERVASIVE. */

            if (!err)
                {
                shm->size = xnheap_max_contiguous(&shm->heapbase);
                shm->addr = xnheap_alloc(&shm->heapbase, shm->size);
                /* Required. */
                memset(shm->addr, '\0', shm->size);
                shm->size -= PAGE_SIZE;
                }
            }
        }
    else if (len != xnheap_size(&shm->heapbase))
        err = EBUSY;

    up(&shm->maplock);

  err_shm_put:
    pse51_shm_put(shm, 1);

    if (!err)
        return 0;

  error:    
    thread_set_errno(err == ENOMEM ? EFBIG : err);
    return -1;
}

/**
 * Map pages of memory.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/mmap.html
 * 
 */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    pse51_shm_map_t *map;
    unsigned desc_flags;
    pse51_desc_t *desc;
    pse51_shm_t *shm;
    void *result;
    int err;
    spl_t s;

    if (xnpod_asynch_p() || !xnpod_root_p())
        {
        err = EPERM;
        goto error;
        }    

    if (!len)
        {
        err = EINVAL;
        goto error;
        }

    if (flags != MAP_SHARED)
        {
        err = ENOTSUP;
        goto error;
        }

    if (((unsigned long) addr) % PAGE_SIZE)
        {
        err = EINVAL;
        goto error;
        }

    xnlock_get_irqsave(&nklock, s);

    shm = pse51_shm_get(&desc, fd, 1);

    if(IS_ERR(shm))
        {
        xnlock_put_irqrestore(&nklock, s);
        err = -PTR_ERR(shm);
        goto error;
        }

    desc_flags = pse51_desc_getflags(desc);
    xnlock_put_irqrestore(&nklock, s);
    
    if ((desc_flags != O_RDWR && desc_flags != O_RDONLY) ||
        ((prot & PROT_WRITE) && desc_flags == O_RDONLY))
        {
        err = EACCES;
        goto err_shm_put;
        }    

    if (down_interruptible(&shm->maplock))
        {
        err = EINTR;
        goto err_shm_put;
        }

    if (!shm->addr || off + len > shm->size)
        {
        err = ENXIO;
        goto err_put_lock;
        }

    map = (pse51_shm_map_t *) xnmalloc(sizeof(*map));

    if (!map)
        {
        err = EAGAIN;
        goto err_put_lock;
        }

    /* Align the heap address on a page boundary. */
    result = (void *) PAGE_ALIGN((u_long)shm->addr);
    map->addr = result = (void *)((char *) result + off);
    map->size = len;
    inith(&map->link);
    prependq(&shm->mappings, &map->link);
    up(&shm->maplock);

    return result;


  err_put_lock:
    up(&shm->maplock);
  err_shm_put:
    pse51_shm_put(shm, 1);
  error:
    thread_set_errno(err);
    return MAP_FAILED;
}    

static pse51_shm_t *pse51_shm_lookup(void *addr)
{
    xnholder_t *holder;
    pse51_shm_t *shm = NULL;
    off_t off;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);
    for (holder = getheadq(&pse51_shmq);
         holder;
         holder = nextq(&pse51_shmq, holder))
        {
        shm = link2shm(holder);

        if (!shm->addr)
            continue;

        off = (off_t)(addr - shm->addr);
        if (off >= 0 && off < shm->size)
            break;
        }

    if (!holder)
        {
        xnlock_put_irqrestore(&nklock, s);
        return NULL;
        }

    xnlock_put_irqrestore(&nklock, s);

    return shm;
}

/**
 * Unmap pages of memory.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/munmap.html
 * 
 */
int munmap(void *addr, size_t len)
{
    pse51_shm_map_t *mapping = NULL;
    xnholder_t *holder;
    pse51_shm_t *shm;
    int err;
    spl_t s;

    if (xnpod_asynch_p() || !xnpod_root_p())
        {
        err = EPERM;
        goto error;
        }    

    if (!len)
        {
        err = EINVAL;
        goto error;
        }

    if (((unsigned long) addr) % PAGE_SIZE)
        {
        err = EINVAL;
        goto error;
        }

    xnlock_get_irqsave(&nklock, s);
    shm = pse51_shm_lookup(addr);
    
    if (!shm)
        {
        xnlock_put_irqrestore(&nklock, s);
        err = EINVAL;
        goto error;
        }

    ++shm->nodebase.refcount;
    xnlock_put_irqrestore(&nklock, s);

    if (down_interruptible(&shm->maplock))
        {
        err = EINTR;
        goto err_shm_put;
        }

    for (holder = getheadq(&shm->mappings);
         holder;
         holder = nextq(&shm->mappings, holder))
        {
        mapping = link2map(holder);

        if (mapping->addr == addr && mapping->size == len)
            break;
        }

    if (!holder)
        {
        xnlock_put_irqrestore(&nklock, s);
        err = EINVAL;
        goto err_up;
        }

    removeq(&shm->mappings, holder);    
    up(&shm->maplock);

    xnfree(mapping);
    pse51_shm_put(shm, 2);
    return 0;

  err_up:
    up (&shm->maplock);
  err_shm_put:
    pse51_shm_put(shm, 1);
  error:
    thread_set_errno(err);
    return -1;
}

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
typedef struct {
    unsigned long uobj;
    struct mm_struct *mm;
    unsigned long kobj;

    xnholder_t link;
#define link2assoc(laddr) \
    (pse51_assoc_t *) ((char *)(laddr) - offsetof(pse51_assoc_t, link))

} pse51_assoc_t;

#ifdef CONFIG_SMP
static xnlock_t pse51_assoc_lock;
#endif /* CONIG_SMP */
pse51_assocq_t pse51_umaps;     /* List of user-space mappings. */
pse51_assocq_t pse51_ufds;      /* List of user-space descriptors. */

int pse51_xnheap_get(xnheap_t **pheap, void *addr)
{
    pse51_shm_t *shm;

    shm = pse51_shm_lookup(addr);

    if (!shm)
        return -EBADF;

    *pheap = &shm->heapbase;
    return 0;
}

static int pse51_assoc_lookup_inner(pse51_assocq_t *q,
                                    pse51_assoc_t **passoc,
                                    struct mm_struct *mm,
                                    u_long uobj)
{
    pse51_assoc_t *assoc;
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&pse51_assoc_lock, s);

    holder = getheadq(q);

    if (!holder)
        {
        /* empty list. */
        xnlock_put_irqrestore(&pse51_assoc_lock, s);
        *passoc = NULL;
        return 0;
        }

    do 
        {
        assoc = link2assoc(holder);
        holder = nextq(q, holder);
        }
    while (holder && (assoc->uobj < uobj ||
                      (assoc->uobj == uobj && assoc->mm < mm)));
    
    if (assoc->mm == mm && assoc->uobj == uobj)
        {
        /* found */
        *passoc = assoc;
        xnlock_put_irqrestore(&pse51_assoc_lock, s);
        return 1;
        }

    /* not found. */
    if (assoc->uobj < uobj || (assoc->uobj == uobj && assoc->mm < mm))
        *passoc = holder ? link2assoc(holder) : NULL;
    else
        *passoc = assoc;

    xnlock_put_irqrestore(&pse51_assoc_lock, s);

    return 0;
}

int pse51_assoc_create(pse51_assocq_t *q,
                       u_long kobj,
                       struct mm_struct *mm,
                       u_long uobj)
{
    pse51_assoc_t *assoc, *next;
    spl_t s;

    xnlock_get_irqsave(&pse51_assoc_lock, s);

    if (pse51_assoc_lookup_inner(q, &next, mm, uobj))
        {
        xnlock_put_irqrestore(&pse51_assoc_lock, s);
        return -EBUSY;
        }

    assoc = (pse51_assoc_t *) xnmalloc(sizeof(*assoc));
    if (!assoc)
        {
        xnlock_put_irqrestore(&pse51_assoc_lock, s);
        return -ENOSPC;
        }
        
    assoc->mm = mm;
    assoc->uobj = uobj;
    assoc->kobj = kobj;
    inith(&assoc->link);
    if (next)
        insertq(q, &next->link, &assoc->link);
    else
        appendq(q, &assoc->link);

    xnlock_put_irqrestore(&pse51_assoc_lock, s);

    return 0;
}

int pse51_assoc_lookup(pse51_assocq_t *q,
                       u_long *kobj,
                       struct mm_struct *mm,
                       u_long uobj,
                       int destroy)
{
    pse51_assoc_t *assoc;
    spl_t s;

    xnlock_get_irqsave(&pse51_assoc_lock, s);

    if (!pse51_assoc_lookup_inner(q, &assoc, mm, uobj))
        {
        xnlock_put_irqrestore(&pse51_assoc_lock, s);
        return -EBADF;
        }

    *kobj = assoc->kobj;

    if (destroy)
        {
        removeq(q, &assoc->link);
        xnfree(assoc);
        }

    xnlock_put_irqrestore(&pse51_assoc_lock, s);

    return 0;
}

void pse51_assocq_destroy(pse51_assocq_t *q, void (*destroy)(u_long kobj))
{
    pse51_assoc_t *assoc;
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&pse51_assoc_lock, s);

    while ((holder = getq(q)))
        {
        assoc = link2assoc(holder);
        if (destroy)
            destroy(assoc->kobj);
        xnfree(assoc);
        }

    xnlock_put_irqrestore(&pse51_assoc_lock, s);
}

#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */    

int pse51_shm_pkg_init(void)
{
    initq(&pse51_shmq);
#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    xnlock_init(&pse51_assoc_lock);
    pse51_assocq_init(&pse51_umaps);
    pse51_assocq_init(&pse51_ufds);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */    

    return 0;
}

void pse51_shm_pkg_cleanup(void)
{
    xnholder_t *holder;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    pse51_assocq_destroy(&pse51_umaps, NULL);
    pse51_assocq_destroy(&pse51_ufds, NULL);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

    while ((holder = getheadq(&pse51_shmq)))
        {
        pse51_shm_t *shm = link2shm(holder);
        pse51_node_t *node;
        spl_t s;

#ifdef CONFIG_XENO_OPT_DEBUG
        xnprintf("POSIX shared memory \"%s\" discarded.\n", shm->nodebase.name);
#endif /* CONFIG_XENO_OPT_DEBUG */
        xnlock_get_irqsave(&nklock, s);
        pse51_node_remove(&node, shm->nodebase.name, PSE51_SHM_MAGIC);
        pse51_shm_destroy(shm, 1);
        xnlock_put_irqrestore(&nklock, s);
        }
}

/*@}*/

EXPORT_SYMBOL(shm_open);
EXPORT_SYMBOL(shm_unlink);
EXPORT_SYMBOL(pse51_shm_close);
EXPORT_SYMBOL(ftruncate);
EXPORT_SYMBOL(mmap);
EXPORT_SYMBOL(munmap);
