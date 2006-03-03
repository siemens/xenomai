/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

/**
 * @ingroup posix
 * @defgroup posix_sem Semaphores services.
 *
 * Semaphores services.
 *
 * Semaphores are counters for resources shared between threads. The basic
 * operations on semaphores are: increment the counter atomically, and wait
 * until the counter is non-null and decrement it atomically.
 *
 * Semaphores have a maximum value past which they cannot be incremented.  The
 * macro @a SEM_VALUE_MAX is defined to be this maximum value.
 *
 *@{*/

#include <stddef.h>
#include <stdarg.h>

#include <posix/registry.h>     /* For named semaphores. */
#include <posix/thread.h>
#include <posix/sem.h>

typedef struct pse51_sem {
    unsigned magic;
    xnsynch_t synchbase;
    xnholder_t link;            /* Link in pse51_semq */

#define link2sem(laddr)                                                 \
    ((pse51_sem_t *)(((char *)(laddr)) - offsetof(pse51_sem_t, link)))

    int value;
} pse51_sem_t;

typedef struct pse51_named_sem {
    pse51_sem_t sembase;        /* Has to be the first member. */
#define sem2named_sem(saddr) ((nsem_t *) (saddr))
    
    pse51_node_t nodebase;
#define node2sem(naddr) \
    ((nsem_t *)((char *)(naddr) - offsetof(nsem_t, nodebase)))

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    xnqueue_t userq;            /* List of user-space bindings. */
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */    

    union __xeno_sem descriptor;
} nsem_t;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
typedef struct pse51_uptr {
    struct mm_struct *mm;
    unsigned refcnt;
    unsigned long uaddr;

    xnholder_t link;

#define link2uptr(laddr) \
    ((pse51_uptr_t *)((char *)(laddr) - offsetof(pse51_uptr_t, link)))
} pse51_uptr_t;
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */    

static xnqueue_t pse51_semq;

static inline int sem_trywait_internal (struct __shadow_sem *shadow)

{
    pse51_sem_t *sem;

    if (shadow->magic != PSE51_SEM_MAGIC
        && shadow->magic != PSE51_NAMED_SEM_MAGIC)
        return EINVAL;

    sem = shadow->sem;
 
    if (sem->value == 0)
        return EAGAIN;

    --sem->value;

    return 0;
}

static void sem_destroy_internal (pse51_sem_t *sem)

{
    removeq(&pse51_semq, &sem->link);    
    if (xnsynch_destroy(&sem->synchbase) == XNSYNCH_RESCHED)
        xnpod_schedule();

    pse51_mark_deleted(sem);
    xnfree(sem);
}

/**
 * Lock a semaphore if it is available.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_trywait.html
 * 
 */
int sem_trywait (sem_t *sm)

{
    struct __shadow_sem *shadow = &((union __xeno_sem *) sm)->shadow_sem;
    int err;
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);

    err = sem_trywait_internal(shadow);

    xnlock_put_irqrestore(&nklock, s);
    
    if (err)
        {
        thread_set_errno(err);
        return -1;
        }

    return 0;
}

static inline int sem_timedwait_internal (struct __shadow_sem *shadow,
                                          xnticks_t to)
{
    pse51_sem_t *sem = shadow->sem;
    xnthread_t *cur;
    int err;

    if (xnpod_unblockable_p())
        return EPERM;

    cur = xnpod_current_thread();

    if ((err = sem_trywait_internal(shadow)) == EAGAIN)
        {
        if((err = clock_adjust_timeout(&to, CLOCK_REALTIME)))
            return err;

        xnsynch_sleep_on(&sem->synchbase, to);
            
        /* Handle cancellation requests. */
        thread_cancellation_point(cur);

        if (xnthread_test_flags(cur, XNRMID))
            return EINVAL;

        if (xnthread_test_flags(cur, XNBREAK))
            return EINTR;
        
        if (xnthread_test_flags(cur, XNTIMEO))
            return ETIMEDOUT;
        }

    return err;
}

/**
 * Lock a semaphore.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_wait.html
 * 
 */
int sem_wait (sem_t *sm)

{
    struct __shadow_sem *shadow = &((union __xeno_sem *) sm)->shadow_sem;
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock, s);
    err = sem_timedwait_internal(shadow, XN_INFINITE);
    xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }
    
    return 0;
}

/**
 * Try during a bounded time to lock a semaphore.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_timedwait.html
 * 
 */
int sem_timedwait (sem_t *sm, const struct timespec *abs_timeout)

{
    struct __shadow_sem *shadow = &((union __xeno_sem *) sm)->shadow_sem;
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock, s);
    err = sem_timedwait_internal(shadow, ts2ticks_ceil(abs_timeout)+1);
    xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }
    
    return 0;
}

/**
 * Unlock a semaphore.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_post.html
 * 
 */
int sem_post (sem_t *sm)

{
    struct __shadow_sem *shadow = &((union __xeno_sem *) sm)->shadow_sem;
    pse51_sem_t *sem;
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);

    if (shadow->magic != PSE51_SEM_MAGIC
        && shadow->magic != PSE51_NAMED_SEM_MAGIC)
        {
        thread_set_errno(EINVAL);
        goto error;
        }

    sem = shadow->sem;

    if (sem->value == SEM_VALUE_MAX)
        {
        thread_set_errno(EAGAIN);
        goto error;
        }

    if(xnsynch_wakeup_one_sleeper(&sem->synchbase) != NULL)
        xnpod_schedule();
    else
        ++sem->value;

    xnlock_put_irqrestore(&nklock, s);

    return 0;

 error:

    xnlock_put_irqrestore(&nklock, s);

    return -1;
}

/**
 * Get the value of a semaphore.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_getvalue.html
 * 
 */
int sem_getvalue (sem_t *sm, int *value)

{
    struct __shadow_sem *shadow = &((union __xeno_sem *) sm)->shadow_sem;
    pse51_sem_t *sem;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (shadow->magic != PSE51_SEM_MAGIC
        && shadow->magic != PSE51_NAMED_SEM_MAGIC)
        {
        xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EINVAL);
        return -1;
        }

    sem = shadow->sem;

    *value = sem->value;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

/* Called with nklock locked, irq off. */
static int pse51_sem_init_inner (pse51_sem_t *sem, int pshared, unsigned value)
{
    if (value > (unsigned) SEM_VALUE_MAX)
        return EINVAL;
    
    sem->magic = PSE51_SEM_MAGIC;
    inith(&sem->link);
    appendq(&pse51_semq, &sem->link);    
    xnsynch_init(&sem->synchbase, XNSYNCH_PRIO);
    sem->value = value;

    return 0;
}


/**
 * Initialize an unnamed semaphore.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_init.html
 * 
 */
int sem_init (sem_t *sm, int pshared, unsigned value)
{
    struct __shadow_sem *shadow = &((union __xeno_sem *) sm)->shadow_sem;
    pse51_sem_t *sem;
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (shadow->magic == PSE51_SEM_MAGIC
        || shadow->magic == PSE51_NAMED_SEM_MAGIC
        || shadow->magic == ~PSE51_NAMED_SEM_MAGIC)
        {
        xnholder_t *holder;
        
        for (holder = getheadq(&pse51_semq); holder;
             holder = nextq(&pse51_semq, holder))
            if (holder == &shadow->sem->link)
                {
                err = EBUSY;
                goto error;
                }
        }

    sem = (pse51_sem_t *) xnmalloc(sizeof(pse51_sem_t));
    if (!sem)
        {
        err = ENOSPC;
        goto error;
        }

    err = pse51_sem_init_inner(sem, pshared, value);
    if (err)
        {
        xnfree(sem);
        goto error;
        }

    shadow->magic = PSE51_SEM_MAGIC;
    shadow->sem = sem;
    xnlock_put_irqrestore(&nklock, s);

    return 0;

  error:
    xnlock_put_irqrestore(&nklock, s);
    thread_set_errno(err);

    return -1;
}

/**
 * Destroy an unnamed semaphore.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_destroy.html
 * 
 */
int sem_destroy (sem_t *sm)

{
    struct __shadow_sem *shadow = &((union __xeno_sem *) sm)->shadow_sem;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (shadow->magic != PSE51_SEM_MAGIC)
        {
        thread_set_errno(EINVAL);
        goto error;
        }
    
    sem_destroy_internal(shadow->sem);
    pse51_mark_deleted(shadow);

    xnlock_put_irqrestore(&nklock, s);

    return 0;

  error:

    xnlock_put_irqrestore(&nklock, s);

    return -1;
}

/**
 * Open a named semaphore.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_open.html
 * 
 */
sem_t *sem_open (const char *name, int oflags, ...)
{
    nsem_t *named_sem;
    pse51_node_t *node;
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock, s);

    err = pse51_node_get(&node, name, PSE51_NAMED_SEM_MAGIC, oflags);

    if (err)
        goto error;

    if (!node)
        {
        unsigned value;
        mode_t mode;
        va_list ap;

        named_sem = (nsem_t *) xnmalloc(sizeof(*named_sem));

        if (!named_sem)
            {
            err = ENOSPC;
            goto error;
            }
        
        va_start(ap, oflags);
        mode = va_arg(ap, int); /* unused */
        value = va_arg(ap, unsigned);
        va_end(ap);

        err = pse51_sem_init_inner(&named_sem->sembase, 1, value);

        if (err)
            {
            xnfree(named_sem);
            goto error;
            }

        err = pse51_node_add(&named_sem->nodebase, name, PSE51_NAMED_SEM_MAGIC);

        if (err)
            {
            xnfree(named_sem);
            goto error;
            }

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
        initq(&named_sem->userq);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
        named_sem->descriptor.shadow_sem.sem = &named_sem->sembase;
        named_sem->sembase.magic = PSE51_NAMED_SEM_MAGIC;
        }
    else
        named_sem = node2sem(node);
    
    /* Set the magic, needed both at creation and when re-opening a semaphore
       that was closed but not unlinked. */
    named_sem->descriptor.shadow_sem.magic = PSE51_NAMED_SEM_MAGIC;

    xnlock_put_irqrestore(&nklock, s);

    return &named_sem->descriptor.native_sem;

  error:

    xnlock_put_irqrestore(&nklock, s);

    thread_set_errno(err);

    return SEM_FAILED;
}

/**
 * Close a named semaphore.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_close.html
 * 
 */
int sem_close (sem_t *sm)
{
    struct __shadow_sem *shadow = &((union __xeno_sem *) sm)->shadow_sem;
    nsem_t *named_sem;
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock, s);

    if(shadow->magic != PSE51_NAMED_SEM_MAGIC)
        {
        err = EINVAL;
        goto error;
        }

    named_sem = sem2named_sem(shadow->sem);
    
    err = pse51_node_put(&named_sem->nodebase);

    if (err)
        goto error;
    
    if (pse51_node_removed_p(&named_sem->nodebase))
        {
        /* unlink was called, and this semaphore is no longer referenced. */
        sem_destroy_internal(&named_sem->sembase);
        pse51_mark_deleted(shadow);
        }
    else if (!pse51_node_ref_p(&named_sem->nodebase))
        /* this semaphore is closed, but not unlinked. */
        pse51_mark_deleted(shadow);

    xnlock_put_irqrestore(&nklock, s);

    return 0;

  error:
    xnlock_put_irqrestore(&nklock, s);

    thread_set_errno(err);

    return -1;
}

/**
 * Unlink a named semaphore.
 *
 * @see http://www.opengroup.org/onlinepubs/000095399/functions/sem_unlink.html
 * 
 */
int sem_unlink (const char *name)
{
    pse51_node_t *node;
    nsem_t *named_sem;
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock, s);

    err = pse51_node_remove(&node, name, PSE51_NAMED_SEM_MAGIC);

    if (err == EINVAL)
        err = ENOENT;

    if (err)
        goto error;

    named_sem = node2sem(node);
    
    if (pse51_node_removed_p(&named_sem->nodebase))
        sem_destroy_internal(&named_sem->sembase);

    xnlock_put_irqrestore(&nklock, s);

    return 0;

  error:
    thread_set_errno(err);

    return -1;
}

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
/* Must be called nklock locked, irq off. */
unsigned long pse51_usem_open (struct __shadow_sem *shadow,
                               struct mm_struct *mm,
                               unsigned long uaddr)
{
    xnholder_t *holder;
    pse51_uptr_t *uptr;
    nsem_t *nsem;

    if (shadow->magic != PSE51_NAMED_SEM_MAGIC)
        return 0;

    nsem = sem2named_sem(shadow->sem);
     
    for(holder = getheadq(&nsem->userq);
        holder;
        holder = nextq(&nsem->userq, holder))
        {
        pse51_uptr_t *uptr = link2uptr(holder);

        if (uptr->mm == mm)
            {
            ++uptr->refcnt;
            return uptr->uaddr;
            }
        }

    uptr = (pse51_uptr_t *) xnmalloc(sizeof(*uptr));
    uptr->mm = mm;
    uptr->uaddr = uaddr;
    uptr->refcnt = 1;
    inith(&uptr->link);
    appendq(&nsem->userq, &uptr->link);
    return uaddr;
}

/* Must be called nklock locked, irq off. */
int pse51_usem_close (struct __shadow_sem *shadow, struct mm_struct *mm)
{
    nsem_t *nsem;
    pse51_uptr_t *uptr = NULL;
    xnholder_t *holder;

    if (shadow->magic != PSE51_NAMED_SEM_MAGIC)
        return -EINVAL;

    nsem = sem2named_sem(shadow->sem);
 
    for(holder = getheadq(&nsem->userq);
        holder;
        holder = nextq(&nsem->userq, holder))
        {
        uptr = link2uptr(holder);

        if (uptr->mm == mm)
            {
            if (--uptr->refcnt)
                return 0;
            break;
            }
        }

    if (!uptr)
        return -EINVAL;

    removeq(&nsem->userq, &uptr->link);
    xnfree(uptr);
    return 1;
}

/* Must be called nklock locked, irq off. */
void pse51_usems_cleanup (pse51_sem_t *sem)
{
    nsem_t *nsem = sem2named_sem(sem);
    xnholder_t *holder;
    
    while((holder = getheadq(&nsem->userq)))
        {
        pse51_uptr_t *uptr = link2uptr(holder);

#ifdef CONFIG_XENO_OPT_DEBUG
        xnprintf("POSIX semaphore \"%s\" binding for user process"
                 " discarded.\n", nsem->nodebase.name);
#endif /* CONFIG_XENO_OPT_DEBUG */

        removeq(&nsem->userq, &uptr->link);
        xnfree(uptr);
        }
}
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

void pse51_sem_pkg_init (void) {

    initq(&pse51_semq);
}

void pse51_sem_pkg_cleanup (void)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    while ((holder = getheadq(&pse51_semq)) != NULL)
        {
        pse51_sem_t *sem = link2sem(holder);

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
        if (sem->magic == PSE51_NAMED_SEM_MAGIC)
            pse51_usems_cleanup(sem);
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */

#ifdef CONFIG_XENO_OPT_DEBUG
        if (sem->magic == PSE51_SEM_MAGIC)
            xnprintf("POSIX semaphore %p discarded.\n",
                     sem);
        else
            xnprintf("POSIX semaphore \"%s\" discarded.\n",
                     sem2named_sem(sem)->nodebase.name);
#endif /* CONFIG_XENO_OPT_DEBUG */
        sem_destroy_internal(sem);
        }

    xnlock_put_irqrestore(&nklock, s);
}

/*@}*/

EXPORT_SYMBOL(pse51_sem_init);
EXPORT_SYMBOL(sem_destroy);
EXPORT_SYMBOL(sem_post);
EXPORT_SYMBOL(sem_trywait);
EXPORT_SYMBOL(sem_wait);
EXPORT_SYMBOL(sem_timedwait);
EXPORT_SYMBOL(sem_getvalue);
EXPORT_SYMBOL(sem_open);
EXPORT_SYMBOL(sem_close);
EXPORT_SYMBOL(sem_unlink);
