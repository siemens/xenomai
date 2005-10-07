/**
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 * @note Copyright (C) 2005 Nextream France S.A.
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

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <rtai/fifo.h>

static RT_FIFO __fifo_table[CONFIG_XENO_OPT_PIPE_NRDEV];

static int __fifo_flush_apc;

static DECLARE_XNQUEUE(__fifo_flush_q);

static inline ssize_t __fifo_flush (RT_FIFO *fifo)

{
    ssize_t nbytes = fifo->fillsz + sizeof(xnpipe_mh_t);
    void *buffer = fifo->buffer;

    fifo->buffer = NULL;
    fifo->fillsz = 0;

    return xnpipe_send(fifo->minor,buffer,nbytes,XNPIPE_NORMAL);
    /* The buffer will be freed by the output handler. */
}

static void __fifo_flush_handler (void *cookie)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    /* Flush all fifos with pending data. */

    while ((holder = getq(&__fifo_flush_q)) != NULL)
	{
	RT_FIFO *fifo = link2rtfifo(holder);
	__clear_bit(0,&fifo->flushable);
	xnlock_put_irqrestore(&nklock,s);
	__fifo_flush(fifo);
	xnlock_get_irqsave(&nklock,s);
	}

    xnlock_put_irqrestore(&nklock,s);
}

#define X_FIFO_HANDLER2(handler) ((int (*)(int, ...))(handler))

static int __fifo_exec_handler (int minor,
				struct xnpipe_mh *mh,
				int retval,
				void *cookie)
{
    RT_FIFO *fifo = __fifo_table + minor;
    int err;

    if (retval >= 0 &&
	fifo->handler != NULL &&
	(err = X_FIFO_HANDLER2(fifo->handler)(minor, 'w') < 0))
	retval = err;

    return retval;
}

static int __fifo_output_handler (int minor,
				  xnpipe_mh_t *mh,
				  int retval,
				  void *cookie)
{
    RT_FIFO *fifo = __fifo_table + minor;
    int err;

    xnfree(mh);

    if (retval >= 0 &&
	fifo->handler != NULL &&
	(err = X_FIFO_HANDLER2(fifo->handler)(minor, 'r') < 0))
	retval = err;

    return retval;
}

int __fifo_pkg_init (void)

{
    int i;

    __fifo_flush_apc = rthal_apc_alloc("fifo_flush",&__fifo_flush_handler,NULL);

    for (i=0; i < CONFIG_XENO_OPT_PIPE_NRDEV; i++) {
	inith(&__fifo_table[i].link);
    }

    if (__fifo_flush_apc < 0)
	return __fifo_flush_apc;

    return 0;
}

void __fifo_pkg_cleanup (void)

{
    rthal_apc_free(__fifo_flush_apc);
}

int rtf_create (unsigned minor, int size)

{
    RT_FIFO *fifo;
    int err;
    spl_t s;

    if (minor >= CONFIG_XENO_OPT_PIPE_NRDEV)
	return -ENODEV;

    fifo = __fifo_table + minor;

    err = xnpipe_connect(minor,
			 &__fifo_output_handler,
			 &__fifo_exec_handler,
			 NULL,
			 fifo);

    xnlock_get_irqsave(&nklock,s);

    ++fifo->refcnt;

    if (err == -EBUSY)
	{
	if (fifo->bufsz < size)
	    {
	    /* Resize the fifo on-the-fly if the specified buffer size
	       for the fifo is larger than the current one; we first
	       flush any pending output. */

	    if (__test_and_clear_bit(0,&fifo->flushable))
		{
		removeq(&__fifo_flush_q,&fifo->link);
		__fifo_flush(fifo);
		} /* Otherwise, there is no currently allocated buffer. */

	    fifo->bufsz = size;
	    }

	goto unlock_and_exit;
	}

    /* <!> We don't pre-allocate the internal buffer unlike the
       original API. */
    fifo->buffer = NULL;
    fifo->bufsz = size;
    fifo->fillsz = 0;
    fifo->flushable = 0;
    fifo->minor = minor;
    fifo->handler = NULL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return 0;
}

int rtf_destroy (unsigned minor)

{
    RT_FIFO *fifo;
    int refcnt;
    spl_t s;

    if (minor >= CONFIG_XENO_OPT_PIPE_NRDEV)
	return -ENODEV;

    fifo = __fifo_table + minor;

    xnlock_get_irqsave(&nklock,s);

    refcnt = fifo->refcnt;

    if (refcnt == 0)
	refcnt = -EINVAL;
    else
	{
	if (--refcnt == 0)
	    {
	    if (__test_and_clear_bit(0,&fifo->flushable))
		{
		removeq(&__fifo_flush_q,&fifo->link);
		xnfree(fifo->buffer);
		}

	    xnpipe_disconnect(minor);
	    }

	fifo->refcnt = refcnt;
	}

    xnlock_put_irqrestore(&nklock,s);

    return refcnt;
}

int rtf_get (unsigned minor,
	     void *buf,
	     int count)
{
    xnpipe_mh_t *msg;
    ssize_t nbytes;
    RT_FIFO *fifo;
    spl_t s;

    if (minor >= CONFIG_XENO_OPT_PIPE_NRDEV)
	return -ENODEV;

    if (count == 0)
	return 0;

    fifo = __fifo_table + minor;

    xnlock_get_irqsave(&nklock,s);

    if (fifo->refcnt == 0)
	{
	nbytes = -EINVAL;
	goto unlock_and_exit;
	}

    nbytes = xnpipe_recv(minor,&msg,XN_NONBLOCK);

    if (nbytes < 0)
	{
	if (nbytes == -EWOULDBLOCK)
	    nbytes = 0;

	goto unlock_and_exit;
	}

    /* <!> Behaviour differs from the original API: we don't scatter
       the received data, so rtf_get() must be passed a buffer large
       enough to collect the largest block of data sent by the
       user-space in a single call to write(). */

    if (count < xnpipe_m_size(msg))
	nbytes = -ENOSPC;
    else if (xnpipe_m_size(msg) > 0)
	memcpy(buf,xnpipe_m_data(msg),xnpipe_m_size(msg));

    /* Zero-sized messages are allowed, so we still need to free the
       message buffer even if no data copy took place. */

    xnfree(msg);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return nbytes;
}

int rtf_put (unsigned minor,
	     const void *buf,
	     int count)
{
    ssize_t outbytes = 0;
    RT_FIFO *fifo;
    size_t n;
    spl_t s;

    if (minor >= CONFIG_XENO_OPT_PIPE_NRDEV)
	return -ENODEV;

    fifo = __fifo_table + minor;

    xnlock_get_irqsave(&nklock,s);

    if (fifo->refcnt == 0)
	{
	outbytes = -EINVAL;
	goto unlock_and_exit;
	}

    while (count > 0)
	{
	if (count >= fifo->bufsz - fifo->fillsz)
	    n = fifo->bufsz - fifo->fillsz;
	else
	    n = count;

	if (n == 0)
	    {
	    ssize_t err = __fifo_flush(fifo);

	    if (__test_and_clear_bit(0,&fifo->flushable))
		removeq(&__fifo_flush_q,&fifo->link);

	    if (err < 0)
		{
		outbytes = err;
		goto unlock_and_exit;
		}

	    continue;
	    }

	if (fifo->buffer == NULL)
	    {
	    fifo->buffer = (xnpipe_mh_t *)xnmalloc(fifo->bufsz + sizeof(xnpipe_mh_t));

	    if (fifo->buffer == NULL)
		{
		outbytes = -ENOMEM;
		goto unlock_and_exit;
		}

	    inith(&fifo->buffer->link);
	    fifo->buffer->size = count;
	    }

	memcpy(xnpipe_m_data(fifo->buffer) + fifo->fillsz,(caddr_t)buf + outbytes,n);
	fifo->fillsz += n;
	outbytes += n;
	count -= n;
	}

    if (fifo->fillsz > 0 && !__test_and_set_bit(0,&fifo->flushable))
	{
	appendq(&__fifo_flush_q,&fifo->link);
	rthal_apc_schedule(__fifo_flush_apc);
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return outbytes;
}

int rtf_reset (unsigned minor)

{
    RT_FIFO *fifo;
    spl_t s;

    if (minor >= CONFIG_XENO_OPT_PIPE_NRDEV)
	return -ENODEV;

    fifo = __fifo_table + minor;

    xnlock_get_irqsave(&nklock,s);

    if (__test_and_clear_bit(0,&fifo->flushable))
	{
	removeq(&__fifo_flush_q,&fifo->link);
	xnfree(fifo->buffer);
	fifo->buffer = NULL;
	fifo->fillsz = 0;
	}

    xnlock_put_irqrestore(&nklock,s);

    return 0;
}

int rtf_create_handler (unsigned minor,
			int (*handler)(unsigned minor))
{
    RT_FIFO *fifo;

    if (minor >= CONFIG_XENO_OPT_PIPE_NRDEV || !handler)
	return -EINVAL;

    fifo = __fifo_table + minor;
    fifo->handler = handler;

    return 0;
}

EXPORT_SYMBOL(rtf_create);
EXPORT_SYMBOL(rtf_destroy);
EXPORT_SYMBOL(rtf_put);
EXPORT_SYMBOL(rtf_get);
EXPORT_SYMBOL(rtf_reset);
EXPORT_SYMBOL(rtf_create_handler);
