/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Linux wrappers.
 */

#ifndef _XENO_ASM_GENERIC_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/config.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)

#include <linux/wrapper.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/moduleparam.h>	/* Use the backport. */

/* Compiler */
#ifndef __attribute_const__
#define __attribute_const__	/* unimplemented */
#endif
#ifndef __restrict__
#define __restrict__		/* unimplemented */
#endif

#define module_param_named(name,var,type,mode)  module_param(var,type,mode)

/* VM */
#define wrap_remap_page_range(vma,from,to,size,prot) ({ \
    vma->vm_flags |= VM_RESERVED; \
    remap_page_range(from,to,size,prot); \
})
#define wrap_switch_mm(prev,next,task)	\
    switch_mm(prev,next,task,(task)->processor)
#define wrap_enter_lazy_tlb(mm,task)	\
    enter_lazy_tlb(mm,task,(task)->processor)
#define pte_offset_kernel(pmd,addr)	pte_offset(pmd,addr)
#define __copy_to_user_inatomic		__copy_to_user
#define __copy_from_user_inatomic	__copy_from_user

/* Seqfiles */
#define SEQ_START_TOKEN ((void *)1)

/* Sched */
#define MAX_RT_PRIO 100
#define task_cpu(p) ((p)->processor)
#ifndef CONFIG_PREEMPT
#define preempt_disable()  do { } while(0)
#define preempt_enable()   do { } while(0)
#endif /* CONFIG_PREEMPT */

/* Signals */
#define wrap_sighand_lock(p)     ((p)->sigmask_lock)
#define wrap_get_sigpending(m,p) sigandsets(m, \
					    &(p)->pending.signal, \
					    &(p)->pending.signal)
/* Wait queues */
#define DEFINE_WAIT(w) DECLARE_WAITQUEUE(w, current)
#define is_sync_wait(wait)  (!(wait) || ((wait)->task))

static inline void prepare_to_wait_exclusive(wait_queue_head_t *q,
					     wait_queue_t *wait,
					     int state)
{
	unsigned long flags;

	wait->flags |= WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue_tail(q, wait);
	if (is_sync_wait(wait))
		set_current_state(state);
	spin_unlock_irqrestore(&q->lock, flags);
}

static inline void finish_wait(wait_queue_head_t *q,
			       wait_queue_t *wait)
{
	unsigned long flags;

	__set_current_state(TASK_RUNNING);
	if (waitqueue_active(q)) {
		spin_lock_irqsave(&q->lock, flags);
		list_del_init(&wait->task_list);
		spin_unlock_irqrestore(&q->lock, flags);
	}
}

/* Workqueues. Some 2.4 ports already provide for a limited emulation
   of workqueue calls in linux/workqueue.h, except DECLARE_WORK(), so
   we define the latter here, and leave the rest in
   compat/linux/workqueue.h. */

#define __WORK_INITIALIZER(n,f,d) {				\
        .list	= { &(n).list, &(n).list },			\
	.sync = 0,						\
	.routine = (f),						\
	.data = (d),						\
}
#define DECLARE_WORK(n,f,d)      struct tq_struct n = __WORK_INITIALIZER(n, f, d)

/* Msleep is unknown before 2.4.28 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,28)
#define msleep(x) do {				 \
	set_current_state(TASK_UNINTERRUPTIBLE); \
	schedule_timeout((x)*(HZ/1000));         \
} while(0)
#endif

/* Types */
typedef enum __kernel_clockid_t {
    CLOCK_REALTIME  =0,
    CLOCK_MONOTONIC =1
} clockid_t;

typedef int timer_t;
typedef int mqd_t;

/* Decls */
struct task_struct;
void show_stack(struct task_struct *task,
		unsigned long *sp);

#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0) */

/* VM */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
#define wrap_remap_page_range(vma,from,to,size,prot)  \
    /* Sets VM_RESERVED | VM_IO | VM_PFNMAP on the vma. */ \
    remap_pfn_range(vma,from,(to) >> PAGE_SHIFT,size,prot)
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10) */
#define wrap_remap_page_range(vma,from,to,size,prot) do { \
    vma->vm_flags |= VM_RESERVED; \
    remap_page_range(vma,from,to,size,prot); \
} while (0)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10) */
#define wrap_switch_mm(prev,next,task)	\
    switch_mm(prev,next,task)
#define wrap_enter_lazy_tlb(mm,task)	\
    enter_lazy_tlb(mm,task)

/* Device registration */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13)
#define DECLARE_DEVCLASS(clname) struct class *clname
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
#define wrap_class_device_create class_device_create
#else /* < 2.6.15 */
#define wrap_class_device_create(c,p,dt,dv,fmt,args...) class_device_create(c,dt,dv,fmt , ##args)
#endif /* >= 2.6.15 */
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define DECLARE_DEVCLASS(clname) struct class_simple *clname
#define wrap_class_device_create(c,p,dt,dv,fmt,args...) class_simple_device_add(c,dt,dv,fmt , ##args)
#define class_create class_simple_create
#define class_device_destroy(a,b) class_simple_device_remove(b)
#define class_destroy class_simple_destroy
#endif  /* >= 2.6.13 */

/* Signals */
#define wrap_sighand_lock(p)     ((p)->sighand->siglock)
#define wrap_get_sigpending(m,p) sigorsets(m, \
					   &(p)->pending.signal, \
					   &(p)->signal->shared_pending.signal)

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0) */

#endif /* _XENO_ASM_GENERIC_WRAPPERS_H */
