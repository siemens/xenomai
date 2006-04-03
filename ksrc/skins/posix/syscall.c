/**
 * @file
 * This file is part of the Xenomai project.
 *
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org> 
 * Copyright (C) 2005 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>
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

#include <posix/syscall.h>
#include <posix/posix.h>
#include <posix/thread.h>
#include <posix/mutex.h>
#include <posix/cond.h>
#include <posix/jhash.h>
#include <posix/mq.h>
#include <posix/intr.h>
#include <posix/registry.h>     /* For PSE51_MAXNAME. */
#include <posix/sem.h>
#include <posix/shm.h>

static int __muxid;

int __pse51_errptd;

struct pthread_jhash {

#define PTHREAD_HASHBITS 8

    pthread_t k_tid;
    struct pse51_hkey hkey;
    struct pthread_jhash *next;
};

static struct pthread_jhash *__jhash_buckets[1<<PTHREAD_HASHBITS]; /* Guaranteed zero */

/* We want to keep the native pthread_t token unmodified for
   Xenomai mapped threads, and keep it pointing at a genuine
   NPTL/LinuxThreads descriptor, so that portions of the POSIX
   interface which are not overriden by Xenomai fall back to the
   original Linux services. If the latter invoke Linux system calls,
   the associated shadow thread will simply switch to secondary exec
   mode to perform them. For this reason, we need an external index to
   map regular pthread_t values to Xenomai's internal thread ids used
   in syscalling the POSIX skin, so that the outer interface can keep
   on using the former transparently. Semaphores and mutexes do not
   have this constraint, since we fully override their respective
   interfaces with Xenomai-based replacements. */

static inline struct pthread_jhash *__pthread_hash (const struct pse51_hkey *hkey,
                                                    pthread_t k_tid)
{
    struct pthread_jhash **bucketp;
    struct pthread_jhash *slot;
    u32 hash;
    spl_t s;

    slot = (struct pthread_jhash *)xnmalloc(sizeof(*slot));

    if (!slot)
        return NULL;

    slot->hkey = *hkey;
    slot->k_tid = k_tid;

    hash = jhash2((u32 *)&slot->hkey,
                  sizeof(slot->hkey)/sizeof(u32),
                  0);

    bucketp = &__jhash_buckets[hash&((1<<PTHREAD_HASHBITS)-1)];

    xnlock_get_irqsave(&nklock,s);
    slot->next = *bucketp;
    *bucketp = slot;
    xnlock_put_irqrestore(&nklock,s);

    return slot;
}

static inline void __pthread_unhash (const struct pse51_hkey *hkey)

{
    struct pthread_jhash **tail, *slot;
    u32 hash;
    spl_t s;

    hash = jhash2((u32 *)hkey,
                  sizeof(*hkey)/sizeof(u32),
                  0);

    tail = &__jhash_buckets[hash&((1<<PTHREAD_HASHBITS)-1)];

    xnlock_get_irqsave(&nklock,s);

    slot = *tail;

    while (slot != NULL &&
           (slot->hkey.u_tid != hkey->u_tid ||
            slot->hkey.mm != hkey->mm))
        {
        tail = &slot->next;
        slot = *tail;
        }

    if (slot)
        *tail = slot->next;

    xnlock_put_irqrestore(&nklock,s);

    if (slot)
        xnfree(slot);
}

static pthread_t __pthread_find (const struct pse51_hkey *hkey)

{
    struct pthread_jhash *slot;
    pthread_t k_tid;
    u32 hash;
    spl_t s;

    hash = jhash2((u32 *)hkey,
                  sizeof(*hkey)/sizeof(u32),
                  0);

    xnlock_get_irqsave(&nklock,s);

    slot = __jhash_buckets[hash&((1<<PTHREAD_HASHBITS)-1)];

    while (slot != NULL &&
           (slot->hkey.u_tid != hkey->u_tid ||
            slot->hkey.mm != hkey->mm))
        slot = slot->next;

    k_tid = slot ? slot->k_tid : NULL;

    xnlock_put_irqrestore(&nklock,s);

    return k_tid;
}

int __pthread_create (struct task_struct *curr, struct pt_regs *regs)

{
    struct sched_param param;
    struct pse51_hkey hkey;
    pthread_attr_t attr;
    pthread_t k_tid;
    int err;

    if (curr->policy != SCHED_FIFO) /* Only allow FIFO for now. */
        return -EINVAL;

    /* We have been passed the pthread_t identifier the user-space
       POSIX library has assigned to our caller; we'll index our
       internal pthread_t descriptor in kernel space on it. */
    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;

    /* Build a default thread attribute, then make sure that a few
       critical fields are set in a compatible fashion wrt to the
       calling context. */

    pthread_attr_init(&attr);
    attr.policy = curr->policy;
    param.sched_priority = curr->rt_priority;
    attr.detachstate = PTHREAD_CREATE_DETACHED;
    attr.schedparam = param;
    attr.fp = 1;
    attr.name = curr->comm;

    err = pthread_create(&k_tid,&attr,NULL,NULL);

    if (err)
        return -err; /* Conventionally, our error codes are negative. */

    err = xnshadow_map(&k_tid->threadbase,NULL);

    if (!err && !__pthread_hash(&hkey,k_tid))
        err = -ENOMEM;

    if (err)
        pse51_thread_abort(k_tid, NULL);
    else
        k_tid->hkey = hkey;
        
    return err;
}

int __pthread_detach (struct task_struct *curr, struct pt_regs *regs)

{ 
    struct pse51_hkey hkey;
    pthread_t k_tid;

    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);

    return -pthread_detach(k_tid);
}

static int __pthread_shadow (struct task_struct *curr,
                             struct pse51_hkey *hkey,
                             struct sched_param *param)
{
    pthread_attr_t attr;
    pthread_t k_tid;
    int err;

    pthread_attr_init(&attr);
    attr.policy = SCHED_FIFO;
    attr.schedparam = *param;
    attr.fp = 1;
    attr.name = curr->comm;

    err = pthread_create(&k_tid,&attr,NULL,NULL);

    if (err)
        return -err;

    err = xnshadow_map(&k_tid->threadbase,NULL);

    if (!err && !__pthread_hash(hkey,k_tid))
        err = -ENOMEM;

    if (err)
        pse51_thread_abort(k_tid, NULL);
    else
        k_tid->hkey = *hkey;
        
    return err;
}

int __pthread_setschedparam (struct task_struct *curr, struct pt_regs *regs)

{ 
    int policy, err, promoted = 0;
    struct sched_param param;
    struct pse51_hkey hkey;
    pthread_t k_tid;

    policy = __xn_reg_arg2(regs);

    if (policy != SCHED_FIFO)
        /* User-space POSIX shadows only support SCHED_FIFO for now. */
        return -EINVAL;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(param)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &param,
                        (void __user *)__xn_reg_arg3(regs),
                        sizeof(param));

    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);

    if (!k_tid && __xn_reg_arg1(regs) == __xn_reg_arg4(regs))
        {
        /* If the syscall applies to "current", and the latter is not
           a Xenomai thread already, then shadow it. */
        err = __pthread_shadow(curr,&hkey,&param);
        promoted = 1;
        }
    else
        err = -pthread_setschedparam(k_tid,policy,&param);

    if (!err)
        __xn_put_user(curr,promoted,(int __user *)__xn_reg_arg5(regs));

    return err;
}

int __sched_yield (struct task_struct *curr, struct pt_regs *regs)

{
    return -sched_yield();
}

int __pthread_make_periodic_np (struct task_struct *curr, struct pt_regs *regs)

{ 
    struct timespec startt, periodt;
    struct pse51_hkey hkey;
    pthread_t k_tid;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(startt)))
        return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(periodt)))
        return -EFAULT;

    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);

    __xn_copy_from_user(curr,
                        &startt,
                        (void __user *)__xn_reg_arg2(regs),
                        sizeof(startt));

    __xn_copy_from_user(curr,
                        &periodt,
                        (void __user *)__xn_reg_arg3(regs),
                        sizeof(periodt));

    return -pthread_make_periodic_np(k_tid,&startt,&periodt);
}

int __pthread_wait_np (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long overruns;
    int err;

    if (__xn_reg_arg1(regs) &&
	!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(overruns)))
	return -EFAULT;

    err = -pthread_wait_np(&overruns);

    if (__xn_reg_arg1(regs) && (err == 0 || err == -ETIMEDOUT))
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&overruns,sizeof(overruns));

    return err;
}

int __pthread_set_mode_np (struct task_struct *curr, struct pt_regs *regs)

{
    xnflags_t clrmask, setmask;
    struct pse51_hkey hkey;
    pthread_t k_tid;
    int err;

    hkey.u_tid = __xn_reg_arg1(regs); /* always pthread_self() */
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);
    clrmask = __xn_reg_arg2(regs);
    setmask = __xn_reg_arg3(regs);

    /* XNTHREAD_SPARE1 is used for primary mode switch. */

    if ((clrmask & ~(XNSHIELD|XNTRAPSW|XNTHREAD_SPARE1)) != 0 ||
        (setmask & ~(XNSHIELD|XNTRAPSW|XNTHREAD_SPARE1)) != 0)
        return -EINVAL;

    err = xnpod_set_thread_mode(&k_tid->threadbase,
                                clrmask & ~XNTHREAD_SPARE1,
                                setmask & ~XNTHREAD_SPARE1);

    if ((clrmask & XNTHREAD_SPARE1) != 0)
        xnshadow_relax(0);

    return err;
}

int __pthread_set_name_np (struct task_struct *curr, struct pt_regs *regs)

{ 
    char name[XNOBJECT_NAME_LEN];
    struct pse51_hkey hkey;
    pthread_t k_tid;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
        return -EFAULT;

    __xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(k_tid, PSE51_THREAD_MAGIC, struct pse51_thread))
        {
        xnlock_put_irqrestore(&nklock, s);
        return -ESRCH;
        }

    strcpy(xnthread_name(&k_tid->threadbase),name);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int __sem_init (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_sem sm, *usm;
    unsigned value;
    int pshared;

    usm = (union __xeno_sem *) __xn_reg_arg1(regs);
    
    if (!__xn_access_ok(curr,VERIFY_WRITE,(void __user *) usm,sizeof(*usm)))
        return -EFAULT;

    pshared = (int)__xn_reg_arg2(regs);
    value = (unsigned)__xn_reg_arg3(regs);

    __xn_copy_from_user(curr,
                        &sm.shadow_sem,
                        (void __user*)&usm->shadow_sem,
                        sizeof(sm.shadow_sem));

    if (sem_init(&sm.native_sem,pshared,value) == -1)
        return -thread_get_errno();

    __xn_copy_to_user(curr,
                      (void __user *)&usm->shadow_sem,
                      &sm.shadow_sem,
                      sizeof(usm->shadow_sem));

    return 0;
}

int __sem_post (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_sem sm, *usm;

    usm = (union __xeno_sem *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) usm,sizeof(*usm)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &sm.shadow_sem,
                        (void __user*)&usm->shadow_sem,
                        sizeof(sm.shadow_sem));

    return sem_post(&sm.native_sem) == 0 ? 0 : -thread_get_errno();
}

int __sem_wait (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_sem sm, *usm;

    usm = (union __xeno_sem *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) usm,sizeof(*usm)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &sm.shadow_sem,
                        (void __user*)&usm->shadow_sem,
                        sizeof(sm.shadow_sem));

    return sem_wait(&sm.native_sem) == 0 ? 0 : -thread_get_errno();
}

int __sem_timedwait (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_sem sm, *usm;
    struct timespec ts;

    usm = (union __xeno_sem *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) usm,sizeof(*usm)))
        return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(ts)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &sm.shadow_sem,
                        (void __user*)&usm->shadow_sem,
                        sizeof(sm.shadow_sem));

    __xn_copy_from_user(curr,
                        &ts,
                        (void __user *)__xn_reg_arg2(regs),
                        sizeof(ts));

    return sem_timedwait(&sm.native_sem, &ts) == 0 ? : -thread_get_errno();
}

int __sem_trywait (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_sem sm, *usm;

    usm = (union __xeno_sem *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) usm,sizeof(*usm)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &sm.shadow_sem,
                        (void __user*)&usm->shadow_sem,
                        sizeof(sm.shadow_sem));

    return sem_trywait(&sm.native_sem) == 0 ? 0 : -thread_get_errno();
}

int __sem_getvalue (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_sem sm, *usm;
    int err, sval;

    usm = (union __xeno_sem *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) usm,sizeof(*usm)))
        return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(sval)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &sm.shadow_sem,
                        (void __user*)&usm->shadow_sem,
                        sizeof(sm.shadow_sem));

    err = sem_getvalue(&sm.native_sem,&sval);

    if (err)
        return -thread_get_errno();

    __xn_copy_to_user(curr,
                      (void __user *)__xn_reg_arg2(regs),
                      &sval,
                      sizeof(sval));
    return 0;
}

int __sem_destroy (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_sem sm, *usm;
    int err;

    usm = (union __xeno_sem *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) usm,sizeof(*usm)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &sm.shadow_sem,
                        (void __user*)&usm->shadow_sem,
                        sizeof(sm.shadow_sem));

    err = sem_destroy(&sm.native_sem);

    if (err)
        return -thread_get_errno();

    __xn_copy_to_user(curr,
                      (void __user *)&usm->shadow_sem,
                      &sm.shadow_sem,
                      sizeof(usm->shadow_sem));

    return 0;
}

int __sem_open (struct task_struct *curr, struct pt_regs *regs)
{
    union __xeno_sem *sm, *usm;
    char name[PSE51_MAXNAME];
    unsigned long uaddr;
    int oflags;
    long len;
    spl_t s;
    
    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(usm)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &usm,
                        (void __user *)__xn_reg_arg1(regs),
                        sizeof(usm));
    
    if (!__xn_access_ok(curr,VERIFY_WRITE,(void __user *)usm,sizeof(*usm)))
        return -EFAULT;

    len = __xn_strncpy_from_user(curr,
                                 name,
                                 (char *) __xn_reg_arg2(regs),
                                 sizeof(name));

    if (len < 0)
        return len;

    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    oflags = __xn_reg_arg3(regs);

    xnlock_get_irqsave(&nklock, s);

    if (!(oflags & O_CREAT))
        sm = (union __xeno_sem *) sem_open(name, oflags);
    else
        sm = (union __xeno_sem *) sem_open(name,
                                           oflags,
                                           (mode_t) __xn_reg_arg4(regs),
                                           (unsigned) __xn_reg_arg5(regs));

    if (sm == SEM_FAILED)
        {
        xnlock_put_irqrestore(&nklock, s);
        return -thread_get_errno();
        }

    uaddr = pse51_usem_open(&sm->shadow_sem, curr->mm, (unsigned long)usm);

    xnlock_put_irqrestore(&nklock, s);

    if (uaddr == (unsigned long) usm)
        /* First binding by this process. */
        __xn_copy_to_user(curr,
                          (void __user *)&usm->shadow_sem,
                          &sm->shadow_sem,
                          sizeof(usm->shadow_sem));
    else
        /* Semaphore already bound by this process in user-space. */
        __xn_copy_to_user(curr,
                          (void __user *)__xn_reg_arg1(regs),
                          &uaddr,
                          sizeof(unsigned long));

    return 0;
}

int __sem_close (struct task_struct *curr, struct pt_regs *regs)
{
    union __xeno_sem sm, *usm;
    int closed, err;
    spl_t s;
    
    usm = (union __xeno_sem *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) usm,sizeof(*usm)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &sm.shadow_sem,
                        (void __user*)&usm->shadow_sem,
                        sizeof(sm.shadow_sem));

    xnlock_get_irqsave(&nklock, s);

    closed = pse51_usem_close(&usm->shadow_sem, curr->mm);

    if (closed < 0)
        {
        xnlock_put_irqrestore(&nklock, s);
        return closed;
        }

    err = sem_close(&sm.native_sem);

    xnlock_put_irqrestore(&nklock, s);

    if (!err)
        {
        __xn_copy_to_user(curr,
                          (void __user *) __xn_reg_arg2(regs),
                          &closed,
                          sizeof(int));

        return 0;
        }

    return -thread_get_errno();
}

int __sem_unlink (struct task_struct *curr, struct pt_regs *regs)
{
    char name[PSE51_MAXNAME];
    long len;
    
    len = __xn_strncpy_from_user(curr,
                                 name,
                                 (char *) __xn_reg_arg1(regs),
                                 sizeof(name));

    if (len < 0)
        return len;

    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    return sem_unlink(name) == 0 ? 0 : -thread_get_errno();
}

int __clock_getres (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec ts;
    clockid_t clock_id;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(ts)))
        return -EFAULT;

    clock_id = __xn_reg_arg1(regs);

    err = clock_getres(clock_id,&ts);

    if (!err)
        __xn_copy_to_user(curr,
                          (void __user *)__xn_reg_arg2(regs),
                          &ts,
                          sizeof(ts));
    return err ? -thread_get_errno() : 0;
}

int __clock_gettime (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec ts;
    clockid_t clock_id;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(ts)))
        return -EFAULT;

    clock_id = __xn_reg_arg1(regs);

    err = clock_gettime(clock_id,&ts);

    if (!err)
        __xn_copy_to_user(curr,
                          (void __user *)__xn_reg_arg2(regs),
                          &ts,
                          sizeof(ts));
    return err ? -thread_get_errno() : 0;
}

int __clock_settime (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec ts;
    clockid_t clock_id;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(ts)))
        return -EFAULT;

    clock_id = __xn_reg_arg1(regs);

    __xn_copy_from_user(curr,
                        &ts,
                        (void __user *)__xn_reg_arg2(regs),
                        sizeof(ts));

    return clock_settime(clock_id,&ts) ? -thread_get_errno() : 0;
}

int __clock_nanosleep (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec rqt, rmt, *rmtp = NULL;
    clockid_t clock_id;
    int flags, err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(rqt)))
        return -EFAULT;

    if (__xn_reg_arg4(regs))
        {
        if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(rmt)))
            return -EFAULT;

        rmtp = &rmt;
        }

    clock_id = __xn_reg_arg1(regs);

    flags = (int)__xn_reg_arg2(regs);

    __xn_copy_from_user(curr,
                        &rqt,
                        (void __user *)__xn_reg_arg3(regs),
                        sizeof(rqt));

    err = clock_nanosleep(clock_id,flags,&rqt,rmtp);

    if (err != EINTR)
        return -err;

    if (rmtp)
        __xn_copy_to_user(curr,
                          (void __user *)__xn_reg_arg4(regs),
                          rmtp,
                          sizeof(*rmtp));
    return -EINTR;
}

int __pthread_mutex_init (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_mutex mx, *umx;
    pthread_mutexattr_t attr;
    int err;

    umx = (union __xeno_mutex *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_WRITE,(void __user *) umx,sizeof(*umx)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &mx.shadow_mutex,
                        (void __user *) &umx->shadow_mutex,
                        sizeof(mx.shadow_mutex));

    /* Recursive + PIP forced. */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutexattr_setprotocol(&attr,PTHREAD_PRIO_INHERIT);
    err = pthread_mutex_init(&mx.native_mutex,&attr);

    if (err)
        return -err;

    __xn_copy_to_user(curr,
                      (void __user *) &umx->shadow_mutex,
                      &mx.shadow_mutex,
                      sizeof(umx->shadow_mutex));
    return 0;
}

int __pthread_mutex_destroy (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_mutex mx, *umx;
    int err;

    umx = (union __xeno_mutex *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) umx,sizeof(*umx)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &mx.shadow_mutex,
                        (void __user *) &umx->shadow_mutex,
                        sizeof(mx.shadow_mutex));

    err = pthread_mutex_destroy(&mx.native_mutex);

    __xn_copy_to_user(curr,
                      (void __user *) &umx->shadow_mutex,
                      &mx.shadow_mutex,
                      sizeof(umx->shadow_mutex));
    return -err;
}

int __pthread_mutex_lock (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_mutex mx, *umx;

    umx = (union __xeno_mutex *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) umx,sizeof(*umx)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &mx.shadow_mutex,
                        (void __user *) &umx->shadow_mutex,
                        sizeof(mx.shadow_mutex));

    return -pse51_mutex_timedlock_break(&mx.shadow_mutex, XN_INFINITE);
}

int __pthread_mutex_timedlock (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_mutex mx, *umx;
    struct timespec ts;

    umx = (union __xeno_mutex *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) umx,sizeof(*umx)))
        return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(ts)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &mx.shadow_mutex,
                        (void __user *) &umx->shadow_mutex,
                        sizeof(mx.shadow_mutex));

    __xn_copy_from_user(curr,
                        &ts,
                        (void __user *)__xn_reg_arg2(regs),
                        sizeof(ts));

    return -pse51_mutex_timedlock_break(&mx.shadow_mutex,ts2ticks_ceil(&ts)+1);
}

int __pthread_mutex_trylock (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_mutex mx, *umx;

    umx = (union __xeno_mutex *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) umx,sizeof(*umx)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &mx.shadow_mutex,
                        (void __user *) &umx->shadow_mutex,
                        sizeof(mx.shadow_mutex));

    return -pthread_mutex_trylock(&mx.native_mutex);
}

int __pthread_mutex_unlock (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_mutex mx, *umx;

    umx = (union __xeno_mutex *) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) umx,sizeof(*umx)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &mx.shadow_mutex,
                        (void __user *) &umx->shadow_mutex,
                        sizeof(mx.shadow_mutex));

    return -pthread_mutex_unlock(&mx.native_mutex);
}

int __pthread_cond_init (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_cond cnd, *ucnd;
    int err;

    ucnd = (union __xeno_cond *) __xn_reg_arg1(regs);
    
    if (!__xn_access_ok(curr,VERIFY_WRITE,(void __user *) ucnd,sizeof(*ucnd)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &cnd.shadow_cond,
                        (void __user *) &ucnd->shadow_cond,
                        sizeof(cnd.shadow_cond));

    /* Always use default attribute. */
    err = pthread_cond_init(&cnd.native_cond,NULL);

    if (err)
        return -err;

    __xn_copy_to_user(curr,
                      (void __user *) &ucnd->shadow_cond,
                      &cnd.shadow_cond,
                      sizeof(ucnd->shadow_cond));
    return 0;
}

int __pthread_cond_destroy (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_cond cnd, *ucnd;
    int err;

    ucnd = (union __xeno_cond *) __xn_reg_arg1(regs);
    
    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) ucnd,sizeof(*ucnd)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &cnd.shadow_cond,
                        (void __user *) &ucnd->shadow_cond,
                        sizeof(cnd.shadow_cond));

    err = pthread_cond_destroy(&cnd.native_cond);

    if (err)
        return -err;

    __xn_copy_to_user(curr,
                      (void __user *) &ucnd->shadow_cond,
                      &cnd.shadow_cond,
                      sizeof(ucnd->shadow_cond));
    return 0;
}

int __pthread_cond_wait (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_cond cnd, *ucnd;
    union __xeno_mutex mx, *umx;

    ucnd = (union __xeno_cond *) __xn_reg_arg1(regs);
    umx = (union __xeno_mutex *) __xn_reg_arg2(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) ucnd,sizeof(*ucnd)))
        return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) umx,sizeof(*umx)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &cnd.shadow_cond,
                        (void __user *) &ucnd->shadow_cond,
                        sizeof(cnd.shadow_cond));

    __xn_copy_from_user(curr,
                        &mx.shadow_mutex,
                        (void __user *) &umx->shadow_mutex,
                        sizeof(mx.shadow_mutex));

    return -pse51_cond_timedwait_internal(&cnd.shadow_cond,
                                          &mx.shadow_mutex,
                                          XN_INFINITE);
}

int __pthread_cond_timedwait (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_cond cnd, *ucnd;
    union __xeno_mutex mx, *umx;
    struct timespec ts;

    ucnd = (union __xeno_cond *) __xn_reg_arg1(regs);
    umx = (union __xeno_mutex *) __xn_reg_arg2(regs);

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) ucnd,sizeof(*ucnd)))
        return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) umx,sizeof(*umx)))
        return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(ts)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &cnd.shadow_cond,
                        (void __user *) &ucnd->shadow_cond,
                        sizeof(cnd.shadow_cond));

    __xn_copy_from_user(curr,
                        &mx.shadow_mutex,
                        (void __user *) &umx->shadow_mutex,
                        sizeof(mx.shadow_mutex));

    __xn_copy_from_user(curr,
                        &ts,
                        (void __user *)__xn_reg_arg3(regs),
                        sizeof(ts));

    return -pse51_cond_timedwait_internal(&cnd.shadow_cond,
                                          &mx.shadow_mutex,
                                          ts2ticks_ceil(&ts)+1);
}

int __pthread_cond_signal (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_cond cnd, *ucnd;

    ucnd = (union __xeno_cond *) __xn_reg_arg1(regs);
    
    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) ucnd,sizeof(*ucnd)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &cnd.shadow_cond,
                        (void __user *) &ucnd->shadow_cond,
                        sizeof(cnd.shadow_cond));

    return -pthread_cond_signal(&cnd.native_cond);
}

int __pthread_cond_broadcast (struct task_struct *curr, struct pt_regs *regs)

{
    union __xeno_cond cnd, *ucnd;

    ucnd = (union __xeno_cond *) __xn_reg_arg1(regs);
    
    if (!__xn_access_ok(curr,VERIFY_READ,(void __user *) ucnd,sizeof(*ucnd)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &cnd.shadow_cond,
                        (void __user *) &ucnd->shadow_cond,
                        sizeof(cnd.shadow_cond));

    return -pthread_cond_broadcast(&cnd.native_cond);
}

int __mq_open (struct task_struct *curr, struct pt_regs *regs)

{
    char name[PSE51_MAXNAME];
    struct mq_attr attr;
    unsigned len;
    mode_t mode;
    int oflags;
    mqd_t q;

    len = __xn_strncpy_from_user(curr,
                                 name,
                                 (const char __user *)__xn_reg_arg1(regs),
                                 sizeof(name));
    if (len <= 0)
        return -EFAULT;
    
    if (len >= sizeof(name))
        return -ENAMETOOLONG;
    
    oflags = __xn_reg_arg2(regs);
    mode = __xn_reg_arg3(regs);

    if (__xn_reg_arg4(regs))
        {
        if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg4(regs),sizeof(attr)))
            return -EFAULT;

        __xn_copy_from_user(curr,&attr,(struct mq_attr *)__xn_reg_arg4(regs),sizeof(attr));
        }
    else
        {
        /* Won't be used, but still, we make sure that it can't be
           used. */
        attr.mq_flags = 0;
        attr.mq_maxmsg = 0;
        attr.mq_msgsize = 0;
        attr.mq_curmsgs = 0;
        }

    q = mq_open(name,oflags,mode,&attr);

    return q == (mqd_t)-1 ? -thread_get_errno() : q;
}

int __mq_close (struct task_struct *curr, struct pt_regs *regs)
{
    mqd_t q;
    int err;
    
    q = (mqd_t) __xn_reg_arg1(regs);
    err = mq_close(q);

    return err ? -thread_get_errno() : 0;
}

int __mq_unlink (struct task_struct *curr, struct pt_regs *regs)
{
    char name[PSE51_MAXNAME];
    unsigned len;
    int err;

    len = __xn_strncpy_from_user(curr,
                                 name,
                                 (const char __user *)__xn_reg_arg1(regs),
                                 sizeof(name));
    if (len <= 0)
        return -EFAULT;

    if (len >= sizeof(name))
        return -ENAMETOOLONG;
    
    err = mq_unlink(name);

    return err ? -thread_get_errno() : 0;
}

int __mq_getattr (struct task_struct *curr, struct pt_regs *regs)
{
    struct mq_attr attr;
    mqd_t q;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(attr)))
        return -EFAULT;

    q = (mqd_t) __xn_reg_arg1(regs);

    err = mq_getattr(q,&attr);

    if (err)
        return -thread_get_errno();

    __xn_copy_to_user(curr,
                      (void __user *)__xn_reg_arg2(regs),
                      &attr,
                      sizeof(attr));
    return 0;
}

int __mq_setattr (struct task_struct *curr, struct pt_regs *regs)
{
    struct mq_attr attr, oattr;
    mqd_t q;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(attr)))
        return -EFAULT;

    q = (mqd_t) __xn_reg_arg1(regs);

    __xn_copy_from_user(curr,&attr,(struct mq_attr *)__xn_reg_arg2(regs),sizeof(attr));

    if (__xn_reg_arg3(regs) &&
        !__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(oattr)))
        return -EFAULT;

    err = mq_setattr(q,&attr,&oattr);

    if (err)
        return -thread_get_errno();

    if (__xn_reg_arg3(regs))
        __xn_copy_to_user(curr,
                          (void __user *)__xn_reg_arg3(regs),
                          &oattr,
                          sizeof(oattr));
    return 0;
}

int __mq_send (struct task_struct *curr, struct pt_regs *regs)
{
    char tmp_buf[PSE51_MQ_FSTORE_LIMIT];
    caddr_t tmp_area;
    unsigned prio;
    size_t len;
    int err;
    mqd_t q;

    q = (mqd_t) __xn_reg_arg1(regs);
    len = (size_t) __xn_reg_arg3(regs);
    prio = __xn_reg_arg4(regs);

    if (len > 0)
        {
        if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),len))
            return -EFAULT;

        /* Try optimizing a bit here: if the message size can fit into
           our local buffer, use the latter; otherwise, take the slow
           path and fetch a larger buffer from the system heap. Most
           messages are expected to be short enough to fit on the
           stack anyway. */

        if (len <= sizeof(tmp_buf))
            tmp_area = tmp_buf;
        else
            {
            tmp_area = xnmalloc(len);

            if (!tmp_area)
                return -ENOMEM;
            }

        __xn_copy_from_user(curr,tmp_area,(void __user *)__xn_reg_arg2(regs),len);
        }
    else
        tmp_area = NULL;

    err = mq_send(q,tmp_area,len,prio);

    if (tmp_area && tmp_area != tmp_buf)
        xnfree(tmp_area);

    return err ? -thread_get_errno() : 0;
}

int __mq_timedsend (struct task_struct *curr, struct pt_regs *regs)
{
    struct timespec timeout, *timeoutp;
    char tmp_buf[PSE51_MQ_FSTORE_LIMIT];
    caddr_t tmp_area;
    unsigned prio;
    size_t len;
    int err;
    mqd_t q;

    if (__xn_reg_arg5(regs) &&
        !__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg5(regs),sizeof(timeout)))
        return -EFAULT;

    q = (mqd_t) __xn_reg_arg1(regs);
    len = (size_t)__xn_reg_arg3(regs);
    prio = __xn_reg_arg4(regs);

    if (len > 0)
        {
        if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),len))
            return -EFAULT;

        if (len <= sizeof(tmp_buf))
            tmp_area = tmp_buf;
        else
            {
            tmp_area = xnmalloc(len);

            if (!tmp_area)
                return -ENOMEM;
            }

        __xn_copy_from_user(curr,tmp_area,(void __user *)__xn_reg_arg2(regs),len);
        }
    else
        tmp_area = NULL;

    if (__xn_reg_arg5(regs))
        {
        __xn_copy_from_user(curr,
                            &timeout,
                            (struct timespec __user *)__xn_reg_arg5(regs),
                            sizeof(timeout));
        timeoutp = &timeout;
        }
    else
        timeoutp = NULL;

    err = mq_timedsend(q,tmp_area,len,prio,timeoutp);

    if (tmp_area && tmp_area != tmp_buf)
        xnfree(tmp_area);

    return err ? -thread_get_errno() : 0;
}

int __mq_receive (struct task_struct *curr, struct pt_regs *regs)
{
    char tmp_buf[PSE51_MQ_FSTORE_LIMIT];
    caddr_t tmp_area;
    unsigned prio;
    ssize_t len;
    mqd_t q;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(len)))
        return -EFAULT;

    __xn_copy_from_user(curr,&len,(ssize_t *)__xn_reg_arg3(regs),sizeof(len));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(prio)))
        return -EFAULT;

    q = (mqd_t) __xn_reg_arg1(regs);

    if (len > 0)
        {
        if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),len))
            return -EFAULT;

        if (len <= sizeof(tmp_buf))
            tmp_area = tmp_buf;
        else
            {
            tmp_area = xnmalloc(len);

            if (!tmp_area)
                return -ENOMEM;
            }
        }
    else
        tmp_area = NULL;

    len = mq_receive(q,tmp_area,len,&prio);

    if (len == -1)
        {
        if (tmp_area && tmp_area != tmp_buf)
            xnfree(tmp_area);

        return -thread_get_errno();
        }

    __xn_copy_to_user(curr,
                      (void __user *)__xn_reg_arg3(regs),
                      &len,
                      sizeof(len));

    if (len > 0)
        __xn_copy_to_user(curr,
                          (void __user *)__xn_reg_arg2(regs),
                          tmp_area,
                          len);

    if (tmp_area && tmp_area != tmp_buf)
        xnfree(tmp_area);

    return 0;
}

int __mq_timedreceive (struct task_struct *curr, struct pt_regs *regs)
{
    struct timespec timeout, *timeoutp;
    char tmp_buf[PSE51_MQ_FSTORE_LIMIT];
    caddr_t tmp_area;
    unsigned prio;
    ssize_t len;
    mqd_t q;

    q = (mqd_t) __xn_reg_arg1(regs);

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(len)))
        return -EFAULT;

    __xn_copy_from_user(curr,&len,(ssize_t *)__xn_reg_arg3(regs),sizeof(len));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(prio)))
        return -EFAULT;

    if (__xn_reg_arg5(regs) &&
        !__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg5(regs),sizeof(timeout)))
        return -EFAULT;

    if (len > 0)
        {
        if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),len))
            return -EFAULT;

        if (len <= sizeof(tmp_buf))
            tmp_area = tmp_buf;
        else
            {
            tmp_area = xnmalloc(len);

            if (!tmp_area)
                return -ENOMEM;
            }
        }
    else
        tmp_area = NULL;

    if (__xn_reg_arg5(regs))
        {
        __xn_copy_from_user(curr,
                            &timeout,
                            (struct timespec __user *)__xn_reg_arg5(regs),
                            sizeof(timeout));
        timeoutp = &timeout;
        }
    else
        timeoutp = NULL;

    len = mq_timedreceive(q,tmp_area,len,&prio,timeoutp);

    if (len == -1)
        {
        if (tmp_area && tmp_area != tmp_buf)
            xnfree(tmp_area);
        
        return -thread_get_errno();
        }

    __xn_copy_to_user(curr,
                      (void __user *)__xn_reg_arg3(regs),
                      &len,
                      sizeof(len));

    if (len > 0)
        __xn_copy_to_user(curr,
                          (void __user *)__xn_reg_arg2(regs),
                          tmp_area,
                          len);

    if (tmp_area && tmp_area != tmp_buf)
        xnfree(tmp_area);

    return 0;
}

int __mq_notify (struct task_struct *curr, struct pt_regs *regs)
{
    struct sigevent sev;
    mqd_t q;

    if (!__xn_access_ok(curr, VERIFY_READ, __xn_reg_arg2(regs), sizeof(sev)))
        return -EFAULT;

    q = (mqd_t) __xn_reg_arg1(regs);

    __xn_copy_from_user(curr, &sev, (char *) __xn_reg_arg2(regs), sizeof(sev));

    if (mq_notify(q, &sev))
        return -thread_get_errno();

    return 0;
}

static int __pse51_intr_handler (xnintr_t *cookie)
{
    struct pse51_interrupt *intr = PTHREAD_IDESC(cookie);

    ++intr->pending;

    if (xnsynch_nsleepers(&intr->synch_base) > 0)
        xnsynch_flush(&intr->synch_base,0);

    if (intr->mode & XN_ISR_PROPAGATE)
        return XN_ISR_PROPAGATE|(intr->mode & XN_ISR_NOENABLE);

    return XN_ISR_HANDLED|(intr->mode & XN_ISR_NOENABLE);
}

int __intr_attach (struct task_struct *curr, struct pt_regs *regs)
{
    struct pse51_interrupt *intr;
    unsigned long handle;
    int err, mode;
    unsigned irq;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(handle)))
        return -EFAULT;

    /* Interrupt line number. */
    irq = (unsigned)__xn_reg_arg2(regs);

    /* Interrupt control mode. */
    mode = (int)__xn_reg_arg3(regs);

    if (mode & ~(XN_ISR_NOENABLE|XN_ISR_PROPAGATE))
        return -EINVAL;

    intr = (struct pse51_interrupt *)xnmalloc(sizeof(*intr));

    if (!intr)
        return -ENOMEM;

    err = pse51_intr_attach(intr,irq,&__pse51_intr_handler,NULL);

    if (err == 0)
        {
        intr->mode = mode;
        handle = (unsigned long)intr;
        __xn_copy_to_user(curr,
                          (void __user *)__xn_reg_arg1(regs),
                          &handle,
                          sizeof(handle));
        }
    else
        xnfree(intr);

    return -err;
}

int __intr_detach (struct task_struct *curr, struct pt_regs *regs)
{
    struct pse51_interrupt *intr = (struct pse51_interrupt *)__xn_reg_arg1(regs);
    int err = pse51_intr_detach(intr);

    if (!err)
        xnfree(intr);

    return -err;
}

int __intr_wait (struct task_struct *curr, struct pt_regs *regs)
{
    struct pse51_interrupt *intr = (struct pse51_interrupt *)__xn_reg_arg1(regs);
    struct timespec ts;
    xnthread_t *thread;
    xnticks_t timeout;
    int err = 0;
    spl_t s;

    if (__xn_reg_arg2(regs))
        {
        if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(ts)))
            return -EFAULT;

        __xn_copy_from_user(curr,
                            &ts,
                            (void __user *)__xn_reg_arg2(regs),
                            sizeof(ts));

        if (ts.tv_sec == 0 && ts.tv_nsec == 0)
            return -EINVAL;

        timeout = ts2ticks_ceil(&ts)+1;
        }
    else
        timeout = XN_INFINITE;

    xnlock_get_irqsave(&nklock,s);

    if (!pse51_obj_active(intr, PSE51_INTR_MAGIC, struct pse51_interrupt))
        {
        xnlock_put_irqrestore(&nklock, s);
        return -EINVAL;
        }

    if (!intr->pending)
        {
        thread = xnpod_current_thread();

        if (xnthread_base_priority(thread) != XNCORE_IRQ_PRIO)
            /* Renice the waiter above all regular threads if needed. */
            xnpod_renice_thread(thread,XNCORE_IRQ_PRIO);

        xnsynch_sleep_on(&intr->synch_base,timeout);
        
        if (xnthread_test_flags(thread,XNRMID))
            err = -EIDRM; /* Interrupt object deleted while pending. */
        else if (xnthread_test_flags(thread,XNTIMEO))
            err = -ETIMEDOUT; /* Timeout.*/
        else if (xnthread_test_flags(thread,XNBREAK))
            err = -EINTR; /* Unblocked.*/
        else
            err = intr->pending;
        }
    else
        err = intr->pending;

    intr->pending = 0;
    
    xnlock_put_irqrestore(&nklock,s);

    return err;
}

int __intr_control (struct task_struct *curr, struct pt_regs *regs)
{
    struct pse51_interrupt *intr = (struct pse51_interrupt *)__xn_reg_arg1(regs);
    int cmd = (int)__xn_reg_arg2(regs);

    return pse51_intr_control(intr,cmd);
}

int __timer_create (struct task_struct *curr, struct pt_regs *regs)
{
    struct sigevent sev;
    timer_t tm;
    int rc;

    if (!__xn_access_ok(curr, VERIFY_READ, __xn_reg_arg2(regs), sizeof(sev)))
        return -EFAULT;

    __xn_copy_from_user(curr, &sev, (char *) __xn_reg_arg2(regs), sizeof(sev));

    rc = timer_create((clockid_t) __xn_reg_arg1(regs), &sev, &tm);

    if(!rc)
        {
        if (!__xn_access_ok(curr, VERIFY_WRITE, __xn_reg_arg3(regs), sizeof(tm)))
            {
            timer_delete(tm);
            return -EFAULT;
            }

        __xn_copy_to_user(curr, (char *) __xn_reg_arg3(regs), &tm, sizeof(tm));
        }

    return rc == 0 ? 0 : -thread_get_errno();
}

int __timer_delete (struct task_struct *curr, struct pt_regs *regs)
{
    int rc;

    rc = timer_delete((timer_t) __xn_reg_arg1(regs));

    return rc == 0 ? 0 : -thread_get_errno();
}

int __timer_settime (struct task_struct *curr, struct pt_regs *regs)
{
    struct itimerspec newv, oldv, *oldvp;
    int rc;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(newv)))
        return -EFAULT;

    oldvp = __xn_reg_arg4(regs) == 0 ? NULL : &oldv;

    __xn_copy_from_user(curr, &newv, (char *) __xn_reg_arg3(regs), sizeof(newv));

    rc = timer_settime((timer_t) __xn_reg_arg1(regs),
                       (int) __xn_reg_arg2(regs),
                       &newv,
                       oldvp);

    if (!rc && oldvp)
        {
        if(!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(oldv)))
            {
            timer_settime((timer_t) __xn_reg_arg1(regs),
                          (int) __xn_reg_arg2(regs),
                          oldvp,
                          NULL);

            return -EFAULT;
            }

        __xn_copy_to_user(curr,
                          (char *) __xn_reg_arg4(regs),
                          oldvp,
                          sizeof(oldv));
        }

    return rc == 0 ? 0 : -thread_get_errno();
}

int __timer_gettime (struct task_struct *curr, struct pt_regs *regs)
{
    struct itimerspec val;
    int rc;

    rc = timer_gettime((timer_t) __xn_reg_arg1(regs), &val);

    if (!rc)
        {
        if(!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(val)))
            return -EFAULT;

        __xn_copy_to_user(curr, (char *) __xn_reg_arg2(regs), &val, sizeof(val));
        }
    
    return rc == 0 ? 0 : -thread_get_errno();
}

int __timer_getoverrun (struct task_struct *curr, struct pt_regs *regs)
{
    int rc;

    rc = timer_getoverrun((timer_t) __xn_reg_arg1(regs));

    return rc >= 0 ? rc : -thread_get_errno();
}

/* shm_open(name, oflag, mode, ufd) */
int __shm_open (struct task_struct *curr, struct pt_regs *regs)
{
    int ufd, kfd, oflag, err;
    char name[PSE51_MAXNAME];
    unsigned len;
    mode_t mode;

    len = __xn_strncpy_from_user(curr,
                                 name,
                                 (const char __user *)__xn_reg_arg1(regs),
                                 sizeof(name));
    if (len <= 0)
        return -EFAULT;

    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    oflag = (int) __xn_reg_arg2(regs);
    mode = (mode_t) __xn_reg_arg3(regs);
    
    kfd = shm_open(name, oflag, mode);

    if (kfd == -1)
        return -thread_get_errno();

    ufd = (int) __xn_reg_arg4(regs);

    err = pse51_assoc_create(&pse51_ufds, (u_long) kfd, curr->mm, (u_long) ufd);
    /* pse51_assoc_create returning an error means that the same mm and user
       file descriptor are already registered. That is impossible. */
    BUG_ON(err);
    return 0;
}

/* shm_unlink(name) */
int __shm_unlink (struct task_struct *curr, struct pt_regs *regs)
{
    char name[PSE51_MAXNAME];
    unsigned len;
    int err;

    len = __xn_strncpy_from_user(curr,
                                 name,
                                 (const char __user *)__xn_reg_arg1(regs),
                                 sizeof(name));
    if (len <= 0)
        return -EFAULT;

    if (len >= sizeof(name))
        return -ENAMETOOLONG;

    err = shm_unlink(name);

    return !err ? 0 : -thread_get_errno();
}

/* shm_close(ufd) */
int __shm_close (struct task_struct *curr, struct pt_regs *regs)
{
    unsigned long kfd;
    int ufd, err;

    ufd = (int) __xn_reg_arg1(regs);

    err = pse51_assoc_lookup(&pse51_ufds, &kfd, curr->mm, (u_long) ufd, 1);

    if (err)
        return err;

    err = close(kfd);

    return !err ? 0 : -thread_get_errno();
}

/* ftruncate(ufd, len) */
int __ftruncate (struct task_struct *curr, struct pt_regs *regs)
{
    unsigned long kfd;
    int ufd, err;
    off_t len;

    ufd = (int) __xn_reg_arg1(regs);
    len = (off_t) __xn_reg_arg2(regs);

    err = pse51_assoc_lookup(&pse51_ufds, &kfd, curr->mm, (u_long) ufd, 0);

    if (err)
        return err;

    err = ftruncate(kfd, len);

    return !err ? 0 : -thread_get_errno();
}

typedef struct {
    void *kaddr;
    unsigned long len;
    xnheap_t *ioctl_cookie;
    unsigned long heapsize;
    unsigned long offset;
} pse51_umap_t;

/* mmap_prologue(len, ufd, off, pse51_umap_t *umap) */
int __mmap_prologue (struct task_struct *curr, struct pt_regs *regs)
{
    unsigned long kfd;
    pse51_umap_t umap;
    int ufd, err;
    size_t len;
    off_t off;

    len = (size_t) __xn_reg_arg1(regs);
    ufd = (int) __xn_reg_arg2(regs);
    off = (off_t) __xn_reg_arg3(regs);

    if(!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(umap)))
        return -EFAULT;    

    err = pse51_assoc_lookup(&pse51_ufds, &kfd, curr->mm, (u_long) ufd, 0);

    if (err)
        return err;

    /* We do not care for the real flags and protection, this mapping is a
       placeholder. */  
    umap.kaddr = mmap(NULL,len,PROT_READ | PROT_WRITE,MAP_SHARED,kfd,off);

    if (umap.kaddr == MAP_FAILED)
        return -thread_get_errno();

    if ((err = pse51_xnheap_get(&umap.ioctl_cookie, umap.kaddr)))
        {
        munmap(umap.kaddr, len);
        return err;
        }

    umap.len = len;
    umap.heapsize = xnheap_size(umap.ioctl_cookie);
    umap.offset = xnheap_mapped_offset(umap.ioctl_cookie, umap.kaddr);

    __xn_copy_to_user(curr,
                      (void __user *)__xn_reg_arg4(regs),
                      &umap,
                      sizeof(umap));

    return 0;
}

/* mmap_epilogue(uaddr, pse51_umap_t *umap) */
int __mmap_epilogue (struct task_struct *curr, struct pt_regs *regs)
{
    pse51_umap_t umap;
    void *uaddr;
    int err;

    uaddr = (void *) __xn_reg_arg1(regs);
 
    if(!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(umap)))
        return -EFAULT;

    __xn_copy_from_user(curr,
                        &umap,
                        (void __user *)__xn_reg_arg2(regs),
                        sizeof(umap));

    if (uaddr == MAP_FAILED)
        {
        munmap(umap.kaddr, umap.len);
        return 0;
        }
    
    err = pse51_assoc_create(&pse51_umaps,
                             (u_long) umap.kaddr,
                             curr->mm,
                             (u_long) uaddr);
    BUG_ON(err);
    return 0;
}

/* munmap_prologue(uaddr, len, &unmap) */
int __munmap_prologue (struct task_struct *curr, struct pt_regs *regs)
{
    struct {
        unsigned long mapsize;
        unsigned long offset;
    } uunmap;
    unsigned long uaddr;
    xnheap_t *heap;
    void *kaddr;
    size_t len;
    int err;

    uaddr = (unsigned long) __xn_reg_arg1(regs);
    len = (size_t) __xn_reg_arg2(regs);
    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(uunmap)))
        return -EFAULT;

    err = pse51_assoc_lookup(&pse51_umaps, (u_long *)&kaddr, curr->mm, uaddr, 0);

    if (err)
        return err;

    err = pse51_xnheap_get(&heap, kaddr);

    if (err)
        return err;
   
    uunmap.mapsize = xnheap_size(heap);
    uunmap.offset = xnheap_mapped_offset(heap, kaddr);
    __xn_copy_to_user(curr,
                      (void __user *)__xn_reg_arg3(regs),
                      &uunmap,
                      sizeof(uunmap));

    return 0;
}

/* munmap_epilogue(uaddr, len) */
int __munmap_epilogue (struct task_struct *curr, struct pt_regs *regs)
{
    unsigned long uaddr;
    void *kaddr;
    size_t len;
    int err;

    uaddr = (unsigned long) __xn_reg_arg1(regs);
    len = (size_t) __xn_reg_arg2(regs);

    err = pse51_assoc_lookup(&pse51_umaps, (u_long *)&kaddr, curr->mm, uaddr, 1);

    if (err)
        return err;

    err = munmap(kaddr, len);

    return !err ? 0 : -thread_get_errno();
}

#if 0
int __itimer_set (struct task_struct *curr, struct pt_regs *regs)
{
    pthread_t thread = pse51_current_thread();
    xnticks_t delay, interval;
    struct itimerval itv;

    if (__xn_reg_arg1(regs))
        {
        if (!__xn_access_ok(curr,VERIFY_READ,(void *)__xn_reg_arg1(regs),sizeof(itv)))
            return -EFAULT;

        __xn_copy_from_user(curr,&itv,(void *)__xn_reg_arg1(regs),sizeof(itv));
        }
    else
        memset(&itv,0,sizeof(itv));

    if (__xn_reg_arg2(regs) &&
        !__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg2(regs),sizeof(itv)))
        return -EFAULT;

    xntimer_stop(&thread->itimer);

    delay = xnshadow_tv2ticks(&itv.it_value);
    interval = xnshadow_tv2ticks(&itv.it_interval);

    if (delay > 0)
        xntimer_start(&thread->itimer,delay,interval);

    if (__xn_reg_arg2(regs))
        {
        interval = xntimer_interval(&thread->itimer);

        if (xntimer_running_p(&thread->itimer))
            {
            delay = xntimer_get_timeout(&thread->itimer);
            
            if (delay == 0)
                delay = 1;
            }
        else
            delay = 0;

        xnshadow_ticks2tv(delay,&itv.it_value);
        xnshadow_ticks2tv(interval,&itv.it_interval);
        __xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&itv,sizeof(itv));
        }

    return 0;
}

int __itimer_get (struct task_struct *curr, struct pt_regs *regs)
{
    pthread_t thread = pse51_current_thread();
    xnticks_t delay, interval;
    struct itimerval itv;

    if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg1(regs),sizeof(itv)))
        return -EFAULT;

    interval = xntimer_interval(&thread->itimer);

    if (xntimer_running_p(&thread->itimer))
        {
        delay = xntimer_get_timeout(&thread->itimer);
        
        if (delay == 0) /* Cannot be negative in this context. */
            delay = 1;
        }
    else
        delay = 0;

    xnshadow_ticks2tv(delay,&itv.it_value);
    xnshadow_ticks2tv(interval,&itv.it_interval);
    __xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&itv,sizeof(itv));

    return 0;
}
#endif

static xnsysent_t __systab[] = {
    [__pse51_thread_create ] = { &__pthread_create, __xn_exec_init },
    [__pse51_thread_detach ] = { &__pthread_detach, __xn_exec_any },
    [__pse51_thread_setschedparam ] = { &__pthread_setschedparam, __xn_exec_conforming },
    [__pse51_sched_yield ] = { &__sched_yield, __xn_exec_primary },
    [__pse51_thread_make_periodic ] = { &__pthread_make_periodic_np, __xn_exec_primary },
    [__pse51_thread_wait] = { &__pthread_wait_np, __xn_exec_primary },
    [__pse51_thread_set_mode] = { &__pthread_set_mode_np, __xn_exec_primary },
    [__pse51_thread_set_name] = { &__pthread_set_name_np, __xn_exec_any },
    [__pse51_sem_init] = { &__sem_init, __xn_exec_any },
    [__pse51_sem_destroy] = { &__sem_destroy, __xn_exec_any },
    [__pse51_sem_post] = { &__sem_post, __xn_exec_any },
    [__pse51_sem_wait] = { &__sem_wait, __xn_exec_primary },
    [__pse51_sem_timedwait] = { &__sem_timedwait, __xn_exec_primary },
    [__pse51_sem_trywait] = { &__sem_trywait, __xn_exec_primary },
    [__pse51_sem_getvalue] = { &__sem_getvalue, __xn_exec_any },
    [__pse51_sem_open] = { &__sem_open, __xn_exec_any },
    [__pse51_sem_close] = { &__sem_close, __xn_exec_any },
    [__pse51_sem_unlink] = { &__sem_unlink, __xn_exec_any },
    [__pse51_clock_getres] = { &__clock_getres, __xn_exec_any },
    [__pse51_clock_gettime] = { &__clock_gettime, __xn_exec_any },
    [__pse51_clock_settime] = { &__clock_settime, __xn_exec_any },
    [__pse51_clock_nanosleep] = { &__clock_nanosleep, __xn_exec_primary },
    [__pse51_mutex_init] = { &__pthread_mutex_init, __xn_exec_any },
    [__pse51_mutex_destroy] = { &__pthread_mutex_destroy, __xn_exec_any },
    [__pse51_mutex_lock] = { &__pthread_mutex_lock, __xn_exec_primary },
    [__pse51_mutex_timedlock] = { &__pthread_mutex_timedlock, __xn_exec_primary },
    [__pse51_mutex_trylock] = { &__pthread_mutex_trylock, __xn_exec_primary },
    [__pse51_mutex_unlock] = { &__pthread_mutex_unlock, __xn_exec_primary },
    [__pse51_cond_init] = { &__pthread_cond_init, __xn_exec_any },
    [__pse51_cond_destroy] = { &__pthread_cond_destroy, __xn_exec_any },
    [__pse51_cond_wait] = { &__pthread_cond_wait, __xn_exec_primary },
    [__pse51_cond_timedwait] = { &__pthread_cond_timedwait, __xn_exec_primary },
    [__pse51_cond_signal] = { &__pthread_cond_signal, __xn_exec_any },
    [__pse51_cond_broadcast] = { &__pthread_cond_broadcast, __xn_exec_any },
    [__pse51_mq_open] = { &__mq_open, __xn_exec_lostage },
    [__pse51_mq_close] = { &__mq_close, __xn_exec_lostage },
    [__pse51_mq_unlink] = { &__mq_unlink, __xn_exec_lostage },
    [__pse51_mq_getattr] = { &__mq_getattr, __xn_exec_any },
    [__pse51_mq_setattr] = { &__mq_setattr, __xn_exec_any },
    [__pse51_mq_send] = { &__mq_send, __xn_exec_primary },
    [__pse51_mq_timedsend] = { &__mq_timedsend, __xn_exec_primary },
    [__pse51_mq_receive] = { &__mq_receive, __xn_exec_primary },
    [__pse51_mq_timedreceive] = { &__mq_timedreceive, __xn_exec_primary },
    [__pse51_mq_notify] = { &__mq_notify, __xn_exec_any },
    [__pse51_intr_attach] = { &__intr_attach, __xn_exec_any },
    [__pse51_intr_detach] = { &__intr_detach, __xn_exec_any },
    [__pse51_intr_wait] = { &__intr_wait, __xn_exec_primary },
    [__pse51_intr_control] = { &__intr_control, __xn_exec_any },
    [__pse51_timer_create] = { &__timer_create, __xn_exec_any },
    [__pse51_timer_delete] = { &__timer_delete, __xn_exec_any },
    [__pse51_timer_settime] = { &__timer_settime, __xn_exec_primary },
    [__pse51_timer_gettime] = { &__timer_gettime, __xn_exec_any },
    [__pse51_timer_getoverrun] = { &__timer_getoverrun, __xn_exec_any },
    [__pse51_shm_open] = {&__shm_open, __xn_exec_lostage },
    [__pse51_shm_unlink] = {&__shm_unlink, __xn_exec_lostage },
    [__pse51_shm_close] = {&__shm_close, __xn_exec_lostage },
    [__pse51_ftruncate] = {&__ftruncate, __xn_exec_lostage },
    [__pse51_mmap_prologue] = {&__mmap_prologue, __xn_exec_lostage },
    [__pse51_mmap_epilogue] = {&__mmap_epilogue, __xn_exec_lostage },
    [__pse51_munmap_prologue] = {&__munmap_prologue, __xn_exec_lostage },
    [__pse51_munmap_epilogue] = {&__munmap_epilogue, __xn_exec_lostage },
};

static void __shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == PSE51_SKIN_MAGIC &&
        testbits(thread->status,XNSHADOW))
        {
        pthread_t k_tid = thread2pthread(thread);
        __pthread_unhash(&k_tid->hkey);
        xnshadow_unmap(thread);
        }
}

int pse51_syscall_init (void)

{
    __muxid =
        xnshadow_register_interface("posix",
                                    PSE51_SKIN_MAGIC,
                                    sizeof(__systab) / sizeof(__systab[0]),
                                    __systab,
                                    NULL);
    if (__muxid < 0)
        return -ENOSYS;

    xnpod_add_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);

    __pse51_errptd = rthal_alloc_ptdkey();
    
    return 0;
}

void pse51_syscall_cleanup (void)

{
    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    xnshadow_unregister_interface(__muxid);
    rthal_free_ptdkey(__pse51_errptd);
}
