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

#include <stddef.h>
#include <stdarg.h>

#include <posix/registry.h>     /* For named semaphores. */
#include <posix/thread.h>
#include <posix/sem.h>

typedef struct pse51_named_sem {
    sem_t sembase;
#define sem2named_sem(saddr) ((nsem_t *) (saddr))
    
    pse51_node_t nodebase;
#define node2sem(naddr) \
    ((nsem_t *)((char *)(naddr) - offsetof(nsem_t, nodebase)))

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    xnqueue_t userq;            /* List of user-space bindings. */
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */    
} nsem_t;

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
typedef struct pse51_uptr {
    pid_t pid;
    unsigned refcnt;
    unsigned long uaddr;

    xnholder_t link;

#define link2uptr(laddr) \
    ((pse51_uptr_t *)((char *)(laddr) - offsetof(pse51_uptr_t, link)))
} pse51_uptr_t;
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */    

static xnqueue_t pse51_semq;

static inline int sem_trywait_internal (sem_t *sem)

{
    if (sem->magic != PSE51_SEM_MAGIC && sem->magic != PSE51_NAMED_SEM_MAGIC)
        return EINVAL;
    
    if (sem->value == 0)
        return EAGAIN;

    --sem->value;

    return 0;
}

static void sem_destroy_internal (sem_t *sem)

{
    removeq(&pse51_semq, &sem->link);    
    if (xnsynch_destroy(&sem->synchbase) == XNSYNCH_RESCHED)
        xnpod_schedule();
    if(sem->magic == PSE51_NAMED_SEM_MAGIC)
        {
        pse51_mark_deleted(sem);
        xnfree(sem2named_sem(sem));
        }
    else
        pse51_mark_deleted(sem);
}

int sem_trywait (sem_t *sem)

{
    int err;
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);

    err = sem_trywait_internal(sem);

    xnlock_put_irqrestore(&nklock, s);
    
    if (err)
        {
        thread_set_errno(err);
        return -1;
        }

    return 0;
}

static inline int sem_timedwait_internal (sem_t *sem, xnticks_t to)

{
    pthread_t cur;
    int err;

    cur = pse51_current_thread();

    if ((err = sem_trywait_internal(sem)) == EAGAIN)
        {
        if((err = clock_adjust_timeout(&to, CLOCK_REALTIME)))
            return err;

        xnsynch_sleep_on(&sem->synchbase, to);
            
        /* Handle cancellation requests. */
        thread_cancellation_point(cur);

        if (xnthread_test_flags(&cur->threadbase, XNRMID))
            return EINVAL;

        if (xnthread_test_flags(&cur->threadbase, XNBREAK))
            return EINTR;
        
        if (xnthread_test_flags(&cur->threadbase, XNTIMEO))
            return ETIMEDOUT;
        }

    return err;
}

int sem_wait (sem_t *sem)

{
    spl_t s;
    int err;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);
    err = sem_timedwait_internal(sem, XN_INFINITE);
    xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }
    
    return 0;
}

int sem_timedwait (sem_t *sem, const struct timespec *abs_timeout)

{
    spl_t s;
    int err;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);
    err = sem_timedwait_internal(sem, ts2ticks_ceil(abs_timeout)+1);
    xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }
    
    return 0;
}

int sem_post (sem_t *sem)

{
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);

    if (sem->magic != PSE51_SEM_MAGIC && sem->magic != PSE51_NAMED_SEM_MAGIC)
        {
        thread_set_errno(EINVAL);
        goto error;
        }

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

int sem_getvalue (sem_t *sem, int *value)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (sem->magic != PSE51_SEM_MAGIC && sem->magic != PSE51_NAMED_SEM_MAGIC)
        {
        xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EINVAL);
        return -1;
        }

    *value = sem->value;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pse51_sem_init (sem_t *sem, int pshared, unsigned int value)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (pshared)
        {
        thread_set_errno(ENOSYS);
        goto error;
        }

    if (value > SEM_VALUE_MAX)
        {
        thread_set_errno(EINVAL);
        goto error;
        }

    sem->magic = PSE51_SEM_MAGIC;
    inith(&sem->link);
    appendq(&pse51_semq, &sem->link);    
    xnsynch_init(&sem->synchbase, XNSYNCH_PRIO);
    sem->value = value;

    xnlock_put_irqrestore(&nklock, s);

    return 0;

  error:

    xnlock_put_irqrestore(&nklock, s);

    return -1;
}

int sem_destroy (sem_t *sem)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (sem->magic != PSE51_SEM_MAGIC)
        {
        thread_set_errno(EINVAL);
        goto error;
        }

    sem_destroy_internal(sem);

    xnlock_put_irqrestore(&nklock, s);

    return 0;

  error:

    xnlock_put_irqrestore(&nklock, s);

    return -1;
}

sem_t *sem_open(const char *name, int oflags, ...)
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
            err = ENOMEM;
            goto error;
            }
        
        va_start(ap, oflags);
        mode = va_arg(ap, int); /* unused */
        value = va_arg(ap, unsigned);
        va_end(ap);

        if (pse51_sem_init(&named_sem->sembase, 0, value))
            {
            xnfree(named_sem);
            err = thread_get_errno();
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
        }
    else
        named_sem = node2sem(node);
    
    /* Set the magic, needed both at creation and when re-opening a semaphore
       that was closed but not unlinked. */
    named_sem->sembase.magic = PSE51_NAMED_SEM_MAGIC;

    xnlock_put_irqrestore(&nklock, s);

    return &named_sem->sembase;

  error:

    xnlock_put_irqrestore(&nklock, s);

    thread_set_errno(err);

    return SEM_FAILED;
}

int sem_close(sem_t *sem)
{
    nsem_t *named_sem;
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock, s);

    if(sem->magic != PSE51_NAMED_SEM_MAGIC)
        {
        err = EINVAL;
        goto error;
        }

    named_sem = sem2named_sem(sem);
    
    err = pse51_node_put(&named_sem->nodebase);

    if (err)
        goto error;
    
    if (pse51_node_removed_p(&named_sem->nodebase))
        /* unlink was called, and this semaphore is no longer referenced. */
        sem_destroy_internal(&named_sem->sembase);
    else if (!pse51_node_ref_p(&named_sem->nodebase))
        /* this semaphore is closed, but not unlinked. */
        pse51_mark_deleted(&named_sem->sembase);

    xnlock_put_irqrestore(&nklock, s);

    return 0;

  error:
    xnlock_put_irqrestore(&nklock, s);

    thread_set_errno(err);

    return -1;
}

int sem_unlink(const char *name)
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
unsigned long pse51_usem_open(sem_t *sem, pid_t pid, unsigned long uaddr)
{
    nsem_t *nsem = sem2named_sem(sem);
    xnholder_t *holder;
    pse51_uptr_t *uptr;

    if (sem->magic != PSE51_NAMED_SEM_MAGIC)
        return 0;

    for(holder = getheadq(&nsem->userq);
        holder;
        holder = nextq(&nsem->userq, holder))
        {
        pse51_uptr_t *uptr = link2uptr(holder);

        if (uptr->pid == pid)
            {
            ++uptr->refcnt;
            return uptr->uaddr;
            }
        }

    uptr = (pse51_uptr_t *) xnmalloc(sizeof(*uptr));
    uptr->pid = pid;
    uptr->uaddr = uaddr;
    uptr->refcnt = 1;
    inith(&uptr->link);
    appendq(&nsem->userq, &uptr->link);
    return uaddr;
}

/* Must be called nklock locked, irq off. */
int pse51_usem_close(sem_t *sem, pid_t pid)
{
    nsem_t *nsem = sem2named_sem(sem);
    pse51_uptr_t *uptr = NULL;
    xnholder_t *holder;

    if (sem->magic != PSE51_NAMED_SEM_MAGIC)
        return -EINVAL;

    for(holder = getheadq(&nsem->userq);
        holder;
        holder = nextq(&nsem->userq, holder))
        {
        uptr = link2uptr(holder);

        if (uptr->pid == pid)
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
void pse51_usems_cleanup(sem_t *sem)
{
    nsem_t *nsem = sem2named_sem(sem);
    xnholder_t *holder;
    
    while((holder = getheadq(&nsem->userq)))
        {
        pse51_uptr_t *uptr = link2uptr(holder);

#ifdef CONFIG_XENO_OPT_DEBUG
        xnprintf("POSIX semaphore \"%s\" binding for user process %d"
                 " discarded.\n", nsem->nodebase.name, uptr->pid);
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
        sem_t *sem = link2sem(holder);

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
