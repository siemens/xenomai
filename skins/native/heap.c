/**
 * @file
 * This file is part of the Xenomai project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
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
 *
 * \ingroup native_heap
 */

/*!
 * \ingroup native
 * \defgroup native_heap Memory heap services.
 *
 * Memory heaps are regions of memory used for dynamic memory
 * allocation in a time-bounded fashion. Blocks of memory are
 * allocated and freed in an arbitrary order and the pattern of
 * allocation and size of blocks is not known until run time.
 *
 * The implementation of the memory allocator follows the algorithm
 * described in a USENIX 1988 paper called "Design of a General
 * Purpose Memory Allocator for the 4.3BSD Unix Kernel" by Marshall
 * K. McKusick and Michael J. Karels.
 *
 * Xenomai memory heaps are built over the nucleus's heap objects, which
 * in turn provide the needed support for sharing a memory area
 * between kernel and user-space using direct memory mapping.
 *
 *@{*/

#include <nucleus/pod.h>
#include <native/task.h>
#include <native/heap.h>
#include <native/registry.h>

#ifdef CONFIG_XENO_NATIVE_EXPORT_REGISTRY

static int __heap_read_proc (char *page,
			     char **start,
			     off_t off,
			     int count,
			     int *eof,
			     void *data)
{
    RT_HEAP *heap = (RT_HEAP *)data;
    char *p = page;
    int len;
    spl_t s;

    p += sprintf(p,"type=%s:size=%lu:used=%lu\n",
		 heap->mode & H_SHARED ? "shared" : "local",
		 xnheap_size(&heap->heap_base),
		 xnheap_used_mem(&heap->heap_base));

    xnlock_get_irqsave(&nklock,s);

    if (xnsynch_nsleepers(&heap->synch_base) > 0)
	{
	xnpholder_t *holder;
	
	/* Pended heap -- dump waiters. */

	holder = getheadpq(xnsynch_wait_queue(&heap->synch_base));

	while (holder)
	    {
	    xnthread_t *sleeper = link2thread(holder,plink);
	    RT_TASK *task = thread2rtask(sleeper);
	    size_t size = task->wait_args.heap.size;
	    p += sprintf(p,"+%s (size=%zd)\n",xnthread_name(sleeper),size);
	    holder = nextpq(xnsynch_wait_queue(&heap->synch_base),holder);
	    }
	}

    xnlock_put_irqrestore(&nklock,s);

    len = (p - page) - off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static RT_OBJECT_PROCNODE __heap_pnode = {

    .dir = NULL,
    .type = "heaps",
    .entries = 0,
    .read_proc = &__heap_read_proc,
    .write_proc = NULL
};

#elif CONFIG_XENO_OPT_NATIVE_REGISTRY

static RT_OBJECT_PROCNODE __heap_pnode = {

    .type = "heaps"
};

#endif /* CONFIG_XENO_NATIVE_EXPORT_REGISTRY */

static void __heap_flush_private (xnheap_t *heap,
				  void *heapmem,
				  u_long heapsize,
				  void *cookie)
{
    xnarch_sysfree(heapmem,heapsize);
}

/*! 
 * \fn int rt_heap_create(RT_HEAP *heap,const char *name,size_t heapsize,int mode);
 * \brief Create a memory heap or a shared memory segment.
 *
 * Initializes a memory heap suitable for time-bounded allocation
 * requests of dynamic memory. Memory heaps can be local to the kernel
 * space, or shared between kernel and user-space.
 *
 * In their simplest form, heaps are only accessible from kernel
 * space, and are merely usable as regular memory allocators.
 *
 * In the shared case, heaps are used as shared memory segments. All
 * allocation requests made through rt_heap_alloc() will then return
 * the same memory block, which will point at the beginning of the
 * heap memory, and cover the entire heap space. This operating mode
 * is specified by passing the H_SHARED flag into the @a mode
 * parameter. By the proper use of a common @a name, all tasks can
 * bind themselves to the same heap and thus share the same memory
 * space, which start address should be subsequently retrieved by a
 * call to rt_heap_alloc().
 *
 * @param heap The address of a heap descriptor Xenomai will use to store
 * the heap-related data.  This descriptor must always be valid while
 * the heap is active therefore it must be allocated in permanent
 * memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * heap. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created heap. Shared heaps must be given a
 * valid name.
 *
 * @param heapsize The size (in bytes) of the block pool which is
 * going to be pre-allocated to the heap. Memory blocks will be
 * claimed and released to this pool.  The block pool is not
 * extensible, so this value must be compatible with the highest
 * memory pressure that could be expected.
 *
 * @param mode The heap creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new heap:
 *
 * - H_FIFO makes tasks pend in FIFO order on the heap when waiting
 * for available blocks.
 *
 * - H_PRIO makes tasks pend in priority order on the heap when
 * waiting for available blocks.
 *
 * - H_SHARED causes the heap to be sharable between kernel and
 * user-space tasks, and make it usable as a shared memory
 * segment. Otherwise, the new heap is only available for kernel-based
 * usage. This flag is implicitely set when the caller is running in
 * user-space. This feature requires the real-time support in
 * user-space to be configured in (CONFIG_XENO_OPT_PERVASIVE).
 *
 * - H_DMA causes the block pool associated to the heap to be
 * allocated in physically contiguous memory, suitable for DMA
 * operations with I/O devices. A 128Kb limit exists for @a heapsize
 * when this flag is passed.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * - -EINVAL is returned if @a heapsize is null, greater than the
 * system limit, or @a name is null or empty for a shared heap.
 *
 * - -ENOMEM is returned if not enough system memory is available to
 * create or register the heap. Additionally, and if H_SHARED has been
 * passed in @a mode, errors while mapping the block pool in the
 * caller's address space might beget this return code too.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * - -ENOSYS is returned if @a mode specifies H_SHARED, but the
 * real-time support in user-space is unavailable.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task (switches to secondary mode)
 *
 * Rescheduling: possible.
 */

int rt_heap_create (RT_HEAP *heap,
		    const char *name,
		    size_t heapsize,
		    int mode)
{
    int err;

    if (!xnpod_root_p())
	return -EPERM;

    if (heapsize == 0)
	return -EINVAL;

    /* Make sure we won't hit trivial argument errors when calling
       xnheap_init(). */

    if (heapsize < 2 * PAGE_SIZE)
	heapsize = 2 * PAGE_SIZE;

    /* Account for the overhead so that the actual free space is large
       enough to match the requested size. Using PAGE_SIZE for large
       shared heaps might reserve way too much useless page map
       memory, but this should never get pathological anyway, since we
       are only consuming 1 byte per page. */

    heapsize += xnheap_overhead(heapsize,PAGE_SIZE);
    heapsize = PAGE_ALIGN(heapsize);

#ifdef __KERNEL__
    if (mode & H_SHARED)
	{
	if (!name || !*name)
	    return -EINVAL;

#ifdef CONFIG_XENO_OPT_PERVASIVE
	err = xnheap_init_shared(&heap->heap_base,
				 heapsize,
				 (mode & H_DMA) ? GFP_DMA : 0);
	if (err)
	    return err;

	heap->cpid = 0;
#else /* !CONFIG_XENO_OPT_PERVASIVE */
	return -ENOSYS;
#endif /* CONFIG_XENO_OPT_PERVASIVE */
	}
    else
#endif /* __KERNEL__ */
	{
	void *heapmem = xnarch_sysalloc(heapsize);

	if (!heapmem)
	    return -ENOMEM;

	err = xnheap_init(&heap->heap_base,
			  heapmem,
			  heapsize,
			  PAGE_SIZE); /* Use natural page size */
	if (err)
	    {
	    xnarch_sysfree(heapmem,heapsize);
	    return err;
	    }
	}

    xnsynch_init(&heap->synch_base,mode & (H_PRIO|H_FIFO));
    heap->handle = 0;  /* i.e. (still) unregistered heap. */
    heap->magic = XENO_HEAP_MAGIC;
    heap->mode = mode;
    heap->shm_block = NULL;
    xnobject_copy_name(heap->name,name);

#ifdef CONFIG_XENO_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name)
        {
	RT_OBJECT_PROCNODE *pnode = &__heap_pnode;
	
	if (!*name)
	    {
	    /* Since this is an anonymous object (empty name on entry)
	       from user-space, it gets registered under an unique
	       internal name but is not exported through /proc. */
	    xnobject_create_name(heap->name,sizeof(heap->name),(void*)heap);
	    pnode = NULL;
	    }

        err = rt_registry_enter(heap->name,heap,&heap->handle,pnode);

        if (err)
            rt_heap_delete(heap);
        }
#endif /* CONFIG_XENO_OPT_NATIVE_REGISTRY */

    return err;
}

/**
 * @fn int rt_heap_delete(RT_HEAP *heap)
 *
 * @brief Delete a real-time heap.
 *
 * Destroy a heap and release all the tasks currently pending on it.
 * A heap exists in the system since rt_heap_create() has been called
 * to create it, so this service must be called in order to destroy it
 * afterwards.
 *
 * @param heap The descriptor address of the affected heap.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a heap is not a heap descriptor.
 *
 * - -EIDRM is returned if @a heap is a deleted heap descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_heap_delete (RT_HEAP *heap)

{
    int err = 0, rc;
    spl_t s;

    if (xnpod_asynch_p())
	return -EPERM;

    xnlock_get_irqsave(&nklock,s);

    heap = xeno_h2obj_validate(heap,XENO_HEAP_MAGIC,RT_HEAP);

    if (!heap)
        {
        err = xeno_handle_error(heap,XENO_HEAP_MAGIC,RT_HEAP);
        goto unlock_and_exit;
        }

#if defined(__KERNEL__) && defined(CONFIG_XENO_OPT_PERVASIVE)
    if (heap->mode & H_SHARED)
	err = xnheap_destroy_shared(&heap->heap_base);
    else
#endif /* __KERNEL__ && CONFIG_XENO_OPT_PERVASIVE */
	err = xnheap_destroy(&heap->heap_base,&__heap_flush_private,NULL);

    if (err)
	goto unlock_and_exit;

    rc = xnsynch_destroy(&heap->synch_base);

#ifdef CONFIG_XENO_OPT_NATIVE_REGISTRY
    if (heap->handle)
        rt_registry_remove(heap->handle);
#endif /* CONFIG_XENO_OPT_NATIVE_REGISTRY */

    xeno_mark_deleted(heap);

    if (rc == XNSYNCH_RESCHED)
        /* Some task has been woken up as a result of the deletion:
           reschedule now. */
        xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_heap_alloc(RT_HEAP *heap,size_t size,RTIME timeout,void **blockp)
 *
 * @brief Allocate a block or return the shared memory base.
 *
 * This service allocates a block from the heap's internal pool, or
 * return the address of the shared memory segment in the caller's
 * address space if the heap is shared. Tasks may wait for some
 * requested amount of memory to become available from local heaps.
 *
 * @param heap The descriptor address of the heap to allocate a block
 * from.
 *
 * @param size The requested size in bytes of the block. If the heap
 * is shared, this value can be either zero, or the same value given
 * to rt_heap_create(). In any case, the same block covering the
 * entire heap space will always be returned to all callers of this
 * service.
 *
 * @param timeout The number of clock ticks to wait for a block of
 * sufficient size to be available from a local heap (see
 * note). Passing TM_INFINITE causes the caller to block indefinitely
 * until some block is eventually available. Passing TM_NONBLOCK
 * causes the service to return immediately without waiting if no
 * block is available on entry. This parameter has no influence if the
 * heap is shared since the entire shared memory space is always
 * available.
 *
 * @param blockp A pointer to a memory location which will be written
 * upon success with the address of the allocated block, or the start
 * address of the shared memory segment. In the former case, the block
 * should be freed using rt_heap_free().
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a heap is not a heap descriptor, or @a
 * heap is shared (i.e. H_SHARED mode) and @a size is non-zero but
 * does not match the actual heap size passed to rt_heap_create().
 *
 * - -EIDRM is returned if @a heap is a deleted heap descriptor.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * TM_NONBLOCK and no block is available within the specified amount
 * of time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to
 * TM_NONBLOCK and no block is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any block was available.
 *
 * - -EPERM is returned if this service should block but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime or
 * scheduler locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK, or the heap is
 *   shared.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation. Operations on shared
 * heaps never start the rescheduling procedure.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

int rt_heap_alloc (RT_HEAP *heap,
		   size_t size,
		   RTIME timeout,
		   void **blockp)
{
    void *block = NULL;
    RT_TASK *task;
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    heap = xeno_h2obj_validate(heap,XENO_HEAP_MAGIC,RT_HEAP);

    if (!heap)
	{
	err = -EINVAL;
	goto unlock_and_exit;
	}

    /* In shared mode, there is only a single allocation returning the
       whole addressable heap space to the user. All users referring
       to this heap are then returned the same block. */

    if (heap->mode & H_SHARED)
	{
	block = heap->shm_block;

	if (!block)
	    {
	    /* It's ok to pass zero for size here, since the requested
	       size is implicitely the whole heap space; but if
	       non-zero is given, it must match the actual heap
	       size. */

	    if (size > 0 && size != xnheap_size(&heap->heap_base))
		{
		err = -EINVAL;
		goto unlock_and_exit;
		}

	    block = heap->shm_block = xnheap_alloc(&heap->heap_base,
						   xnheap_max_contiguous(&heap->heap_base));
	    }

	if (block)
	    goto unlock_and_exit;

	err = -ENOMEM;	/* This should never happen. Paranoid. */
	goto unlock_and_exit;
	}

    block = xnheap_alloc(&heap->heap_base,size);

    if (block)
	goto unlock_and_exit;

    if (timeout == TM_NONBLOCK)
	{
	err = -EWOULDBLOCK;
	goto unlock_and_exit;
	}

    if (xnpod_unblockable_p())
	{
	err = -EPERM;
	goto unlock_and_exit;
	}

    task = xeno_current_task();
    task->wait_args.heap.size = size;
    task->wait_args.heap.block = NULL;
    xnsynch_sleep_on(&heap->synch_base,timeout);

    if (xnthread_test_flags(&task->thread_base,XNRMID))
	err = -EIDRM; /* Heap deleted while pending. */
    else if (xnthread_test_flags(&task->thread_base,XNTIMEO))
	err = -ETIMEDOUT; /* Timeout.*/
    else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	err = -EINTR; /* Unblocked.*/
    else
	block = task->wait_args.heap.block;

 unlock_and_exit:

    *blockp = block;

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_heap_free(RT_HEAP *heap,void *block)
 *
 * @brief Free a block.
 *
 * This service releases a block to the heap's internal pool. If some
 * task is currently waiting for a block so that it's pending request
 * could be satisfied as a result of the release, it is immediately
 * resumed.
 *
 * If the heap is shared (i.e. H_SHARED mode), this service leads to a
 * null-effect and always returns successfully.
 *
 * @param heap The address of the heap descriptor to which the block
 * @a block belong.
 *
 * @param block The address of the block to free.
 *
 * @return 0 is returned upon success, or -EINVAL if @a block is not a
 * valid block previously allocated by the rt_heap_alloc() service.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_heap_free (RT_HEAP *heap,
		  void *block)
{
    int err, nwake;
    spl_t s;

    if (block == NULL)
	return -EINVAL;

    xnlock_get_irqsave(&nklock,s);

    heap = xeno_h2obj_validate(heap,XENO_HEAP_MAGIC,RT_HEAP);

    if (!heap)
        {
        err = xeno_handle_error(heap,XENO_HEAP_MAGIC,RT_HEAP);
        goto unlock_and_exit;
        }
    
    if (heap->mode & H_SHARED)	/* No-op if shared. */
	{
	err = 0;
	goto unlock_and_exit;
	}

    err = xnheap_free(&heap->heap_base,block);

    if (!err && xnsynch_nsleepers(&heap->synch_base) > 0)
	{
	xnpholder_t *holder, *nholder;
	
	nholder = getheadpq(xnsynch_wait_queue(&heap->synch_base));
	nwake = 0;

	while ((holder = nholder) != NULL)
	    {
	    RT_TASK *sleeper = thread2rtask(link2thread(holder,plink));
	    void *block;

	    block = xnheap_alloc(&heap->heap_base,
			       sleeper->wait_args.heap.size);
	    if (block)
		{
		nholder = xnsynch_wakeup_this_sleeper(&heap->synch_base,holder);
		sleeper->wait_args.heap.block = block;
		nwake++;
		}
	    else
		nholder = nextpq(xnsynch_wait_queue(&heap->synch_base),holder);
	    }

	if (nwake > 0)
	    xnpod_schedule();
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_heap_inquire(RT_HEAP *heap, RT_HEAP_INFO *info)
 *
 * @brief Inquire about a heap.
 *
 * Return various information about the status of a given heap.
 *
 * @param heap The descriptor address of the inquired heap.
 *
 * @param info The address of a structure the heap information will
 * be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a heap is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a heap is a deleted queue descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_heap_inquire (RT_HEAP *heap,
		     RT_HEAP_INFO *info)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    heap = xeno_h2obj_validate(heap,XENO_HEAP_MAGIC,RT_HEAP);

    if (!heap)
        {
        err = xeno_handle_error(heap,XENO_HEAP_MAGIC,RT_HEAP);
        goto unlock_and_exit;
        }
    
    strcpy(info->name,heap->name);
    info->nwaiters = xnsynch_nsleepers(&heap->synch_base);
    info->heapsize = xnheap_size(&heap->heap_base);
    info->mode = heap->mode;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_heap_bind(RT_HEAP *heap,const char *name,RTIME timeout)
 *
 * @brief Bind to a shared heap.
 *
 * This user-space only service retrieves the uniform descriptor of a
 * given shared Xenomai heap identified by its symbolic name. If the heap
 * does not exist on entry, this service blocks the caller until a
 * heap of the given name is created.
 *
 * @param name A valid NULL-terminated name which identifies the
 * heap to bind to.
 *
 * @param heap The address of a heap descriptor retrieved by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @param timeout The number of clock ticks to wait for the
 * registration to occur (see note). Passing TM_INFINITE causes the
 * caller to block indefinitely until the object is
 * registered. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if the object is not registered on
 * entry.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EFAULT is returned if @a heap or @a name is referencing invalid
 * memory.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the retrieval has completed.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the searched object is not registered on entry.
 *
 * - -ETIMEDOUT is returned if the object cannot be retrieved within
 * the specified amount of time.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime or
 * scheduler locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

/**
 * @fn int rt_heap_unbind(RT_HEAP *heap)
 *
 * @brief Unbind from a shared heap.
 *
 * This user-space only service unbinds the calling task from the heap
 * object previously retrieved by a call to rt_heap_bind().
 *
 * Unbinding from a heap when it is no more needed is especially
 * important in order to properly release the mapping resources used
 * to attach the shared heap memory to the caller's address space.
 *
 * @param heap The address of a heap descriptor to unbind from.
 *
 * @return 0 is always returned.
 *
 * This service can be called from:
 *
 * - User-space task.
 *
 * Rescheduling: never.
 */

int __heap_pkg_init (void)

{
    return 0;
}

void __heap_pkg_cleanup (void)

{
}

/*@}*/

EXPORT_SYMBOL(rt_heap_create);
EXPORT_SYMBOL(rt_heap_delete);
EXPORT_SYMBOL(rt_heap_alloc);
EXPORT_SYMBOL(rt_heap_free);
EXPORT_SYMBOL(rt_heap_inquire);
