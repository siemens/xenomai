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
 * \ingroup pipe
 */

/*!
 * \ingroup native
 * \defgroup pipe Message pipe services.
 *
 * Message pipe services.
 *
 * Message pipes are an improved replacement for the legacy
 * RT-FIFOS. A message pipe is a two-way communication channel between
 * Xenomai tasks and standard Linux processes using regular file I/O
 * operations on a pseudo-device. Pipes can be operated in a
 * message-oriented fashion so that message boundaries are preserved,
 * and also in byte streaming mode from real-time to standard Linux
 * processes for optimal throughput.
 *
 * Xenomai tasks open their side of the pipe using the rt_pipe_create()
 * service; standard Linux processes do the same by opening one of the
 * /dev/rtpN special devices, where N is the minor number agreed upon
 * between both ends of each pipe. Additionally, named pipes are
 * available through the registry support, which automatically creates
 * a symbolic link from entries under /proc/xenomai/registry/pipes/ to
 * the appropriate special device file.
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <native/registry.h>
#include <native/pipe.h>

static xnheap_t *__pipe_heap = &kheap;

static int __pipe_flush_apc;

static DECLARE_XNQUEUE(__pipe_flush_q);

#ifdef CONFIG_XENO_NATIVE_EXPORT_REGISTRY

static ssize_t __pipe_link_proc (char *buf,
				 int count,
				 void *data)
{
    RT_PIPE *pipe = (RT_PIPE *)data;
    return snprintf(buf,count,"/dev/rtp%d",pipe->minor);
}

static RT_OBJECT_PROCNODE __pipe_pnode = {

    .dir = NULL,
    .type = "pipes",
    .entries = 0,
    .link_proc = &__pipe_link_proc,
};

#elif CONFIG_XENO_OPT_NATIVE_REGISTRY

static RT_OBJECT_PROCNODE __pipe_pnode = {

    .type = "pipes"
};

#endif /* CONFIG_XENO_NATIVE_EXPORT_REGISTRY */

static inline ssize_t __pipe_flush (RT_PIPE *pipe)

{
    ssize_t nbytes = pipe->fillsz + sizeof(RT_PIPE_MSG);
    void *buffer = pipe->buffer;

    pipe->buffer = NULL;
    pipe->fillsz = 0;

    return xnpipe_send(pipe->minor,buffer,nbytes,P_NORMAL);
    /* The buffer will be freed by the output handler. */
}

static void __pipe_flush_handler (void *cookie)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    /* Flush all pipes with pending messages. */

    while ((holder = getq(&__pipe_flush_q)) != NULL)
	{
	RT_PIPE *pipe = link2rtpipe(holder);
	__clear_bit(0,&pipe->flushable);
	xnlock_put_irqrestore(&nklock,s);
	__pipe_flush(pipe);	/* Cannot do anything upon error here. */
	xnlock_get_irqsave(&nklock,s);
	}

    xnlock_put_irqrestore(&nklock,s);
}

static void *__pipe_alloc_handler (int bminor,
				   size_t size,
				   void *cookie)
{
    /* Allocate memory for the incoming message. */
    return xnheap_alloc(__pipe_heap,size);
}

static int __pipe_output_handler (int bminor,
				  xnpipe_mh_t *mh,
				  int retval,
				  void *cookie)
{
    /* Free memory from output/discarded message. */
    xnheap_free(__pipe_heap,mh);
    return retval;
}

int __pipe_pkg_init (void)

{
    __pipe_flush_apc = rthal_apc_alloc("pipe_flush",&__pipe_flush_handler,NULL);

    if (__pipe_flush_apc < 0)
	return __pipe_flush_apc;

    return 0;
}

void __pipe_pkg_cleanup (void)

{
    rthal_apc_free(__pipe_flush_apc);
}

/**
 * @fn int rt_pipe_create(RT_PIPE *pipe,const char *name,int minor)
 * @brief Create a message pipe.
 *
 * This service opens a bi-directional communication channel allowing
 * data exchange between Xenomai tasks and standard Linux
 * processes. Pipes natively preserve message boundaries, but can also
 * be used in byte stream mode from Xenomai tasks to standard Linux
 * processes.
 *
 * rt_pipe_create() always returns immediately, even if no Linux
 * process has opened the associated special device file yet. On the
 * contrary, the non real-time side could block upon attempt to open
 * the special device file until rt_pipe_create() is issued on the
 * same pipe from a Xenomai task, unless O_NONBLOCK has been specified to
 * the open(2) system call.
 *
 * @param pipe The address of a pipe descriptor Xenomai will use to store
 * the pipe-related data.  This descriptor must always be valid while
 * the pipe is active therefore it must be allocated in permanent
 * memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * message pipe. When non-NULL and non-empty, this string is copied to
 * a safe place into the descriptor, and passed to the registry
 * package if enabled for indexing the created pipe.
 *
 * Named pipes are supported through the use of the registry. When the
 * registry support is enabled, passing a valid @a name parameter when
 * creating a message pipe subsequently allows standard Linux
 * processes to follow a symbolic link from
 * /proc/xenomai/registry/pipes/@a name in order to reach the associated
 * special device (i.e. /dev/rtp*), so that the specific @a minor
 * information does not need to be known from those processes for
 * opening the proper device file. In such a case, both sides of the
 * pipe only need to agree upon a symbolic name to refer to the same
 * data path, which is especially useful whenever the @a minor number
 * is picked up dynamically using an adaptive algorithm, depending on
 * the current system configuration.
 *
 * @param minor The minor number of the device associated with the pipe.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get enough dynamic
 * memory from the global real-time heap in order to register the
 * pipe.
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * - -ENODEV is returned if @a minor is not a valid minor number for
 * the pipe special device (i.e. /dev/rtp*).
 *
 * - -EBUSY is returned if @a minor is already open.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_pipe_create (RT_PIPE *pipe,
		    const char *name,
		    int minor)
{
    int err;

    if (xnpod_asynch_p())
	return -EPERM;

    pipe->minor = minor;
    pipe->buffer = NULL;
    pipe->fillsz = 0;
    pipe->flushable = 0;
    pipe->handle = 0;    /* i.e. (still) unregistered pipe. */
    pipe->magic = XENO_PIPE_MAGIC;
    xnobject_copy_name(pipe->name,name);

    err = xnpipe_connect(minor,
			 &__pipe_output_handler,
			 NULL,
			 &__pipe_alloc_handler,
			 pipe);
    if (err)
	return err;

#ifdef CONFIG_XENO_OPT_PERVASIVE
    pipe->cpid = 0;
#endif /* CONFIG_XENO_OPT_PERVASIVE */

#ifdef CONFIG_XENO_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name)
        {
	RT_OBJECT_PROCNODE *pnode = &__pipe_pnode;
	
	if (!*name)
	    {
	    /* Since this is an anonymous object (empty name on entry)
	       from user-space, it gets registered under an unique
	       internal name but is not exported through /proc. */
	    xnobject_create_name(pipe->name,sizeof(pipe->name),(void*)pipe);
	    pnode = NULL;
	    }
	    
        err = rt_registry_enter(pipe->name,pipe,&pipe->handle,pnode);

        if (err)
            rt_pipe_delete(pipe);
        }
#endif /* CONFIG_XENO_OPT_NATIVE_REGISTRY */

    return err;
}

/**
 * @fn int rt_pipe_delete(RT_PIPE *pipe)
 *
 * @brief Delete a message pipe.
 *
 * This service deletes a pipe previously created by rt_pipe_create().
 * Data pending for transmission to non real-time processes are lost.
 *
 * @param pipe The descriptor address of the affected pipe.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF can be returned if @a pipe is scrambled.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_pipe_delete (RT_PIPE *pipe)

{
    int err;
    spl_t s;

    if (xnpod_asynch_p())
	return -EPERM;

    xnlock_get_irqsave(&nklock,s);

    pipe = xeno_h2obj_validate(pipe,XENO_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	err = xeno_handle_error(pipe,XENO_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    if (__test_and_clear_bit(0,&pipe->flushable))
	{
	/* Purge data waiting for flush. */
	removeq(&__pipe_flush_q,&pipe->link);
	rt_pipe_free(pipe->buffer);
	}

    err = xnpipe_disconnect(pipe->minor);

#ifdef CONFIG_XENO_OPT_NATIVE_REGISTRY
    if (pipe->handle)
        rt_registry_remove(pipe->handle);
#endif /* CONFIG_XENO_OPT_NATIVE_REGISTRY */

    xeno_mark_deleted(pipe);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn ssize_t rt_pipe_receive(RT_PIPE *pipe,RT_PIPE_MSG **msgp,RTIME timeout)
 *
 * @brief Receive a message from a pipe.
 *
 * This service retrieves the next message written to the associated
 * special device in user-space. rt_pipe_receive() always preserves
 * message boundaries, which means that all data sent through the same
 * write(2) operation to the special device will be gathered in a
 * single message by this service. This service differs from
 * rt_pipe_read() in that it returns a pointer to the internal buffer
 * holding the message, which improves performances by saving a data
 * copy to a user-provided buffer, especially when large messages are
 * involved.
 *
 * Unless otherwise specified, the caller is blocked for a given
 * amount of time if no data is immediately available on entry.
 *
 * @param pipe The descriptor address of the pipe to receive from.
 *
 * @param msgp A pointer to a memory location which will be written
 * upon success with the address of the received message. Once
 * consumed, the message space should be freed using rt_pipe_free().
 * The application code can retrieve the actual data and size carried
 * by the message by respectively using the P_MSGPTR() and P_MSGSIZE()
 * macros.
 *
 * @param timeout The number of clock ticks to wait for some message
 * to arrive (see note). Passing TM_INFINITE causes the caller to
 * block indefinitely until some data is eventually available. Passing
 * TM_NONBLOCK causes the service to return immediately without
 * waiting if no data is available on entry.
 *
 * @return The number of read bytes available from the received
 * message is returned upon success; this value will be equal to
 * P_MSGSIZE(*msgp). Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * TM_NONBLOCK and no data is available within the specified amount of
 * time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no data is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any data was available.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime or
 * scheduler locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

ssize_t rt_pipe_receive (RT_PIPE *pipe,
			 RT_PIPE_MSG **msgp,
			 RTIME timeout)
{
    ssize_t n;
    spl_t s;

    if (timeout != TM_NONBLOCK && xnpod_unblockable_p())
	return -EPERM;

    xnlock_get_irqsave(&nklock,s);

    pipe = xeno_h2obj_validate(pipe,XENO_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	n = xeno_handle_error(pipe,XENO_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    n = xnpipe_recv(pipe->minor,msgp,timeout);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return n;
}

/**
 * @fn ssize_t rt_pipe_read(RT_PIPE *pipe,void *buf,size_t size,RTIME timeout)
 *
 * @brief Read a message from a pipe.
 *
 * This service retrieves the next message written to the associated
 * special device in user-space. rt_pipe_read() always preserves
 * message boundaries, which means that all data sent through the same
 * write(2) operation to the special device will be gathered in a
 * single message by this service. This services differs from
 * rt_pipe_receive() in that it copies back the payload data to a
 * user-defined memory area, instead of returning a pointer to the
 * internal message buffer holding such data.
 *
 * Unless otherwise specified, the caller is blocked for a given
 * amount of time if no data is immediately available on entry.
 *
 * @param pipe The descriptor address of the pipe to read from.
 *
 * @param buf A pointer to a memory location which will be written
 * upon success with the read message contents.
 *
 * @param size The count of bytes from the received message to read up
 * into @a buf. If @a size is lower than the actual message size,
 * -ENOSPC is returned since the incompletely received message would
 * be lost. If @a size is zero, this call returns immediately with no
 * other action.
 *
 * @param timeout The number of clock ticks to wait for some message
 * to arrive (see note). Passing TM_INFINITE causes the caller to
 * block indefinitely until some data is eventually available. Passing
 * TM_NONBLOCK causes the service to return immediately without
 * waiting if no data is available on entry.
 *
 * @return The number of read bytes copied to the @a buf is returned
 * upon success. Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * TM_NONBLOCK and no data is available within the specified amount of
 * time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no data is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any data was available.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime or
 * scheduler locked).
 *
 * - -ENOSPC is returned if @a size is not large enough to collect the
 * message data.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
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

ssize_t rt_pipe_read (RT_PIPE *pipe,
		      void *buf,
		      size_t size,
		      RTIME timeout)
{
    RT_PIPE_MSG *msg;
    ssize_t nbytes;

    if (size == 0)
	return 0;

    nbytes = rt_pipe_receive(pipe,&msg,timeout);

    if (nbytes < 0)
	return nbytes;

    if (size < P_MSGSIZE(msg))
	nbytes = -ENOSPC;
    else if (P_MSGSIZE(msg) > 0)
	memcpy(buf,P_MSGPTR(msg),P_MSGSIZE(msg));

    /* Zero-sized messages are allowed, so we still need to free the
       message buffer even if no data copy took place. */

    rt_pipe_free(msg);

    return nbytes;
}

 /**
 * @fn ssize_t rt_pipe_send(RT_PIPE *pipe,RT_PIPE_MSG *msg,size_t size,int mode)
 *
 * @brief Send a message through a pipe.
 *
 * This service writes a complete message to be received from the
 * associated special device. rt_pipe_send() always preserves message
 * boundaries, which means that all data sent through a single call of
 * this service will be gathered in a single read(2) operation from
 * the special device. This service differs from rt_pipe_write() in
 * that it accepts a canned message buffer, instead of a pointer to
 * the raw data to be sent. This call is useful whenever the caller
 * wants to prepare the message contents separately from its sending,
 * which does not require to have all the data to be sent available at
 * once but allows for incremental updates of the message, and also
 * saves a message copy, since rt_pipe_send() deals internally with
 * message buffers.
 *
 * @param pipe The descriptor address of the pipe to send to.
 *
 * @param msg The address of the message to be sent.  The message
 * space must have been allocated using the rt_pipe_alloc() service.
 * Once passed to rt_pipe_send(), the memory pointed to by @a msg is
 * no more under the control of the application code and thus should
 * not be referenced by it anymore; deallocation of this memory will
 * be automatically handled as needed. As a special exception, @a msg
 * can be NULL and will not be dereferenced if @a size is zero.
 *
 * @param size The size in bytes of the message (payload data
 * only). Zero is a valid value, in which case the service returns
 * immediately without sending any message.
 *
 * Additionally, rt_pipe_send() causes any data buffered by
 * rt_pipe_stream() to be flushed prior to sending the message. For
 * this reason, rt_pipe_send() can return a non-zero byte count to the
 * caller if some pending data has been flushed, even if @a size was
 * zero on entry.
 *
 * @param mode A set of flags affecting the operation:
 *
 * - P_URGENT causes the message to be prepended to the output
 * queue, ensuring a LIFO ordering.
 *
 * - P_NORMAL causes the message to be appended to the output
 * queue, ensuring a FIFO ordering.
 *
 * @return Upon success, this service returns @a size if the latter is
 * non-zero, or the number of bytes flushed otherwise. Upon error, one
 * of the following error codes is returned:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EPIPE is returned if the associated special device is not yet
 * open.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */

ssize_t rt_pipe_send (RT_PIPE *pipe,
		      RT_PIPE_MSG *msg,
		      size_t size,
		      int mode)
{
    ssize_t n = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    pipe = xeno_h2obj_validate(pipe,XENO_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	n = xeno_handle_error(pipe,XENO_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    if (__test_and_clear_bit(0,&pipe->flushable))
	{
	removeq(&__pipe_flush_q,&pipe->link);
	n = __pipe_flush(pipe);

	if (n < 0)
	    goto unlock_and_exit;
	}

    if (size > 0)
	/* We need to add the size of the message header here. */
	n = xnpipe_send(pipe->minor,msg,size + sizeof(RT_PIPE_MSG),mode);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return n <= 0 ? n : n - sizeof(RT_PIPE_MSG);
}

 /**
 * @fn ssize_t rt_pipe_write(RT_PIPE *pipe,const void *buf,size_t size,int mode)
 *
 * @brief Write a message to a pipe.
 *
 * This service writes a complete message to be received from the
 * associated special device. rt_pipe_write() always preserves message
 * boundaries, which means that all data sent through a single call of
 * this service will be gathered in a single read(2) operation from
 * the special device. This service differs from rt_pipe_send() in
 * that it accepts a pointer to the raw data to be sent, instead of a
 * canned message buffer. This call is useful whenever the caller does
 * not need to prepare the message contents separately from its
 * sending.
 *
 * @param pipe The descriptor address of the pipe to write to.
 *
 * @param buf The address of the first data byte to send. The
 * data will be copied to an internal buffer before transmission.
 *
 * @param size The size in bytes of the message (payload data
 * only). Zero is a valid value, in which case the service returns
 * immediately without sending any message.
 *
 * Additionally, rt_pipe_write() causes any data buffered by
 * rt_pipe_stream() to be flushed prior to sending the message. For
 * this reason, rt_pipe_write() can return a non-zero byte count to
 * the caller if some pending data has been flushed, even if @a size
 * was zero on entry.
 *
 * @param mode A set of flags affecting the operation:
 *
 * - P_URGENT causes the message to be prepended to the output
 * queue, ensuring a LIFO ordering.
 *
 * - P_NORMAL causes the message to be appended to the output
 * queue, ensuring a FIFO ordering.
 *
 * @return Upon success, this service returns @a size if the latter is
 * non-zero, or the number of bytes flushed otherwise. Upon error, one
 * of the following error codes is returned:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EPIPE is returned if the associated special device is not yet
 * open.
 *
 * - -ENOMEM is returned if not enough buffer space is available to
 * complete the operation.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
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

ssize_t rt_pipe_write (RT_PIPE *pipe,
		       const void *buf,
		       size_t size,
		       int mode)
{
    RT_PIPE_MSG *msg;
    ssize_t nbytes;

    if (size == 0)
	/* Try flushing the streaming buffer in any case. */
	return rt_pipe_send(pipe,NULL,0,mode);

    msg = rt_pipe_alloc(size);
	
    if (!msg)
	return -ENOMEM;

    memcpy(P_MSGPTR(msg),buf,size);

    nbytes = rt_pipe_send(pipe,msg,size,mode);

    if (nbytes != size)
	/* If the operation failed, we need to free the message buffer
	   by ourselves. */
	rt_pipe_free(msg);

    return nbytes;
}

/**
 * @fn ssize_t rt_pipe_stream(RT_PIPE *pipe,const void *buf,size_t size)
 *
 * @brief Stream bytes to a pipe.
 *
 * This service writes a sequence of bytes to be received from the
 * associated special device. Unlike rt_pipe_send(), this service does
 * not preserve message boundaries. Instead, an internal buffer is
 * filled on the fly with the data. The actual sending may be delayed
 * until the internal buffer is full, or the Linux kernel is
 * re-entered after the real-time system enters a quiescent state.
 *
 * Data buffers sent by the rt_pipe_stream() service are always
 * transmitted in FIFO order (i.e. P_NORMAL mode).
 *
 * @param pipe The descriptor address of the pipe to write to.
 *
 * @param buf The address of the first data byte to send. The
 * data will be copied to an internal buffer before transmission.
 *
 * @param size The size in bytes of the buffer. Zero is a valid value,
 * in which case the service returns immediately without buffering any
 * data.
 *
 * @return The number of sent bytes upon success; this value will be
 * equal to @a size. Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EPIPE is returned if the associated special device is not yet
 * open.
 *
 * - -ENOMEM is returned if not enough buffer space is available to
 * complete the operation.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * - -ENOSYS is returned if the byte streaming mode has been disabled
 * at configuration time by nullifying the size of the pipe buffer
 * (see CONFIG_XENO_OPT_NATIVE_PIPE_BUFSZ).
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

ssize_t rt_pipe_stream (RT_PIPE *pipe,
			const void *buf,
			size_t size)
{
    ssize_t outbytes = 0;
    size_t n;
    spl_t s;

#if CONFIG_XENO_OPT_NATIVE_PIPE_BUFSZ <= 0
    return -ENOSYS;
#else /* CONFIG_XENO_OPT_NATIVE_PIPE_BUFSZ > 0 */

    xnlock_get_irqsave(&nklock,s);

    pipe = xeno_h2obj_validate(pipe,XENO_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	outbytes = xeno_handle_error(pipe,XENO_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    while (size > 0)
	{
	if (size >= CONFIG_XENO_OPT_NATIVE_PIPE_BUFSZ - pipe->fillsz)
	    n = CONFIG_XENO_OPT_NATIVE_PIPE_BUFSZ - pipe->fillsz;
	else
	    n = size;

	if (n == 0)
	    {
	    ssize_t err = __pipe_flush(pipe);

	    if (__test_and_clear_bit(0,&pipe->flushable))
		removeq(&__pipe_flush_q,&pipe->link);

	    if (err < 0)
		{
		outbytes = err;
		goto unlock_and_exit;
		}

	    continue;
	    }

	if (pipe->buffer == NULL)
	    {
	    pipe->buffer = rt_pipe_alloc(CONFIG_XENO_OPT_NATIVE_PIPE_BUFSZ);

	    if (pipe->buffer == NULL)
		{
		outbytes = -ENOMEM;
		goto unlock_and_exit;
		}
	    }

	memcpy(P_MSGPTR(pipe->buffer) + pipe->fillsz,(caddr_t)buf + outbytes,n);
	pipe->fillsz += n;
	outbytes += n;
	size -= n;
	}

    /* The flushable bit is not that elegant, but we must make sure
       that we won't enqueue the pipe descriptor twice in the flush
       queue, but we still have to enqueue it before the virq is made
       pending if necessary since it could preempt a Linux-based
       caller, so... */

    if (pipe->fillsz > 0 && !__test_and_set_bit(0,&pipe->flushable))
	{
	appendq(&__pipe_flush_q,&pipe->link);
	rthal_apc_schedule(__pipe_flush_apc);
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return outbytes;
#endif /* CONFIG_XENO_OPT_NATIVE_PIPE_BUFSZ <= 0 */
}

/**
 * @fn ssize_t rt_pipe_flush(RT_PIPE *pipe)
 *
 * @brief Flush the pipe.
 *
 * This service flushes any pending data buffered by rt_pipe_stream().
 * This operation makes the data available for reading from the
 * associated special device.
 *
 * @param pipe The descriptor address of the pipe to flush.
 *
 * @return The number of bytes flushed upon success. Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EPIPE is returned if the associated special device is not yet
 * open.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */

ssize_t rt_pipe_flush (RT_PIPE *pipe)

{
    ssize_t n = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    pipe = xeno_h2obj_validate(pipe,XENO_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	n = xeno_handle_error(pipe,XENO_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    if (__test_and_clear_bit(0,&pipe->flushable))
	{
	removeq(&__pipe_flush_q,&pipe->link);
	n = __pipe_flush(pipe);
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return n <= 0 ? n : n - sizeof(RT_PIPE_MSG);
}

/**
 * @fn RT_PIPE_MSG *rt_pipe_alloc(size_t size)
 *
 * @brief Allocate a message pipe buffer.
 *
 * This service allocates a message buffer from the system heap which
 * can be subsequently filled by the caller then passed to
 * rt_pipe_send() for sending. The beginning of the available data
 * area of @a size contiguous bytes is accessible from P_MSGPTR(msg).
 *
 * @param size The requested size in bytes of the buffer. This value
 * should represent the size of the payload data.
 *
 * @return The address of the allocated message buffer upon success,
 * or NULL if the allocation fails.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

RT_PIPE_MSG *rt_pipe_alloc (size_t size)

{
    RT_PIPE_MSG *msg = (RT_PIPE_MSG *)xnheap_alloc(__pipe_heap,size + sizeof(RT_PIPE_MSG));

    if (msg)
	{
	inith(&msg->link);
	msg->size = size;
	}

    return msg;
}

/**
 * @fn int rt_pipe_free(RT_PIPE_MSG *msg)
 *
 * @brief Free a message pipe buffer.
 *
 * This service releases a message buffer returned by
 * rt_pipe_receive() to the system heap.
 *
 * @param msg The address of the message buffer to free.
 *
 * @return 0 is returned upon success, or -EINVAL if @a msg is not a
 * valid message buffer previously allocated by the rt_pipe_alloc()
 * service.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

int rt_pipe_free (RT_PIPE_MSG *msg)
{
    return xnheap_free(__pipe_heap,msg);
}

/*@}*/

EXPORT_SYMBOL(rt_pipe_create);
EXPORT_SYMBOL(rt_pipe_delete);
EXPORT_SYMBOL(rt_pipe_receive);
EXPORT_SYMBOL(rt_pipe_send);
EXPORT_SYMBOL(rt_pipe_read);
EXPORT_SYMBOL(rt_pipe_write);
EXPORT_SYMBOL(rt_pipe_stream);
EXPORT_SYMBOL(rt_pipe_flush);
EXPORT_SYMBOL(rt_pipe_alloc);
EXPORT_SYMBOL(rt_pipe_free);
