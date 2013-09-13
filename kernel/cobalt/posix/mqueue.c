/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
 * @ingroup cobalt
 * @defgroup cobalt_mq Message queues services.
 *
 * Message queues services.
 *
 * A message queue allow exchanging data between real-time
 * threads. For a POSIX message queue, maximum message length and
 * maximum number of messages are fixed when it is created with
 * mq_open().
 *
 *@{*/

#include <stdarg.h>
#include <linux/fs.h>
#include <cobalt/kernel/select.h>
#include "internal.h"
#include "thread.h"
#include "signal.h"
#include "timer.h"
#include "mqueue.h"
#include "clock.h"

#define COBALT_MSGMAX		65536
#define COBALT_MSGSIZEMAX	(16*1024*1024)
#define COBALT_MSGPRIOMAX 	32768

struct mq_attr {
	long mq_flags;
	long mq_maxmsg;
	long mq_msgsize;
	long mq_curmsgs;
};

struct cobalt_mq {
	cobalt_node_t nodebase;
#define node2mq(naddr) container_of(naddr,  cobalt_mq_t, nodebase)

	struct xnsynch receivers;
	struct xnsynch senders;
	size_t memsize;
	char *mem;
	struct list_head queued;
	struct list_head avail;
	int nrqueued;

	/* mq_notify */
	struct siginfo si;
	mqd_t target_qd;
	struct cobalt_thread *target;

	struct mq_attr attr;
	struct list_head link;	/* link in mqq */

	DECLARE_XNSELECT(read_select);
	DECLARE_XNSELECT(write_select);
};

typedef struct cobalt_mq cobalt_mq_t;

struct cobalt_msg {
	struct list_head link;
	unsigned int prio;
	size_t len;
	char data[0];
};

static struct list_head cobalt_mqq;

struct cobalt_mqwait_context {
	struct xnthread_wait_context wc;
	struct cobalt_msg *msg;
};

static struct mq_attr default_attr = {
      .mq_maxmsg = 10,
      .mq_msgsize = 8192,
};

static inline struct cobalt_msg *mq_msg_alloc(cobalt_mq_t *mq)
{
	if (list_empty(&mq->avail))
		return NULL;

	return list_get_entry(&mq->avail, struct cobalt_msg, link);
}

static inline void mq_msg_free(cobalt_mq_t *mq, struct cobalt_msg * msg)
{
	list_add(&msg->link, &mq->avail); /* For earliest re-use of the block. */
}

static inline int mq_init(cobalt_mq_t *mq, const struct mq_attr *attr)
{
	unsigned i, msgsize, memsize;
	char *mem;

	if (attr == NULL)
		attr = &default_attr;
	else {
		if (attr->mq_maxmsg <= 0 || attr->mq_msgsize <= 0)
			return -EINVAL;
		if (attr->mq_maxmsg > COBALT_MSGMAX)
			return -EINVAL;
		if (attr->mq_msgsize > COBALT_MSGSIZEMAX)
			return -EINVAL;
	}

	msgsize = attr->mq_msgsize + sizeof(struct cobalt_msg);

	/* Align msgsize on natural boundary. */
	if ((msgsize % sizeof(unsigned long)))
		msgsize +=
		    sizeof(unsigned long) - (msgsize % sizeof(unsigned long));

	memsize = msgsize * attr->mq_maxmsg;
	memsize = PAGE_ALIGN(memsize);
	if (get_order(memsize) > MAX_ORDER)
		return -ENOSPC;

	mem = alloc_pages_exact(memsize, GFP_KERNEL);
	if (mem == NULL)
		return -ENOSPC;

	mq->memsize = memsize;
	INIT_LIST_HEAD(&mq->queued);
	mq->nrqueued = 0;
	xnsynch_init(&mq->receivers, XNSYNCH_PRIO | XNSYNCH_NOPIP, NULL);
	xnsynch_init(&mq->senders, XNSYNCH_PRIO | XNSYNCH_NOPIP, NULL);
	mq->mem = mem;

	/* Fill the pool. */
	INIT_LIST_HEAD(&mq->avail);
	for (i = 0; i < attr->mq_maxmsg; i++) {
		struct cobalt_msg *msg = (struct cobalt_msg *) (mem + i * msgsize);
		mq_msg_free(mq, msg);
	}

	mq->attr = *attr;
	mq->target = NULL;
	xnselect_init(&mq->read_select);
	xnselect_init(&mq->write_select);

	return 0;
}

struct lostage_memfree {
	struct ipipe_work_header work; /* Must be first. */
	void *mem;
	size_t memsize;
};

static void lostage_mq_memfree(struct ipipe_work_header *work)
{
	struct lostage_memfree *rq;

	rq = container_of(work, struct lostage_memfree, work);
	free_pages_exact(rq->mem, rq->memsize);
}

static inline void mq_destroy(cobalt_mq_t *mq)
{
	struct lostage_memfree freework = {
		.work = {
			.size = sizeof(freework),
			.handler = lostage_mq_memfree,
		},
		.mem = mq->mem,
		.memsize = mq->memsize,
	};
	int resched;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	resched = (xnsynch_destroy(&mq->receivers) == XNSYNCH_RESCHED);
	resched = (xnsynch_destroy(&mq->senders) == XNSYNCH_RESCHED) || resched;
	list_del(&mq->link);
	xnlock_put_irqrestore(&nklock, s);
	xnselect_destroy(&mq->read_select);
	xnselect_destroy(&mq->write_select);
	ipipe_post_work_root(&freework, work);

	if (resched)
		xnsched_run();
}

/**
 * Open a message queue.
 *
 * This service establishes a connection between the message queue named @a name
 * and the calling context (kernel-space as a whole, or user-space process).
 *
 * One of the following values should be set in @a oflags:
 * - O_RDONLY, meaning that the returned queue descriptor may only be used for
 *   receiving messages;
 * - O_WRONLY, meaning that the returned queue descriptor may only be used for
 *   sending messages;
 * - O_RDWR, meaning that the returned queue descriptor may be used for both
 *   sending and receiving messages.
 *
 * If no message queue named @a name exists, and @a oflags has the @a O_CREAT
 * bit set, the message queue is created by this function, taking two more
 * arguments:
 * - a @a mode argument, of type @b mode_t, currently ignored;
 * - an @a attr argument, pointer to an @b mq_attr structure, specifying the
 *   attributes of the new message queue.
 *
 * If @a oflags has the two bits @a O_CREAT and @a O_EXCL set and the message
 * queue alread exists, this service fails.
 *
 * If the O_NONBLOCK bit is set in @a oflags, the mq_send(), mq_receive(),
 * mq_timedsend() and mq_timedreceive() services return @a -1 with @a errno set
 * to EAGAIN instead of blocking their caller.
 *
 * The following arguments of the @b mq_attr structure at the address @a attr
 * are used when creating a message queue:
 * - @a mq_maxmsg is the maximum number of messages in the queue (128 by
 *   default);
 * - @a mq_msgsize is the maximum size of each message (128 by default).
 *
 * @a name may be any arbitrary string, in which slashes have no particular
 * meaning. However, for portability, using a name which starts with a slash and
 * contains no other slash is recommended.
 *
 * @param name name of the message queue to open;
 *
 * @param oflags flags.
 *
 * @return a message queue descriptor on success;
 * @return -1 with @a errno set if:
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - EEXIST, the bits @a O_CREAT and @a O_EXCL were set in @a oflags and the
 *   message queue already exists;
 * - ENOENT, the bit @a O_CREAT is not set in @a oflags and the message queue
 *   does not exist;
 * - ENOSPC, allocation of system memory failed, or insufficient memory exists
 *   in the system heap to create the queue, try increasing
 *   CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EPERM, attempting to create a message queue from an invalid context;
 * - EINVAL, the @a attr argument is invalid;
 * - EMFILE, too many descriptors are currently open.
 *
 * @par Valid contexts:
 * When creating a message queue, only the following contexts are valid:
 * - kernel module initialization or cleanup routine;
 * - user-space thread (Xenomai threads switch to secondary mode).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_open.html">
 * Specification.</a>
 *
 */
static mqd_t mq_open(const char *name, int oflags, ...)
{
	struct mq_attr *attr;
	cobalt_node_t *node;
	cobalt_desc_t *desc;
	cobalt_mq_t *mq;
	mode_t mode;
	va_list ap;
	spl_t s;
	int err;

	xnlock_get_irqsave(&nklock, s);
	err = -cobalt_node_get(&node, name, COBALT_MQ_MAGIC, oflags);
	xnlock_put_irqrestore(&nklock, s);
	if (err)
		goto error;

	if (node) {
		mq = node2mq(node);
		goto got_mq;
	}

	/* Here, we know that we must create a message queue. */
	mq = xnmalloc(sizeof(*mq));
	if (mq == NULL) {
		err = -ENOSPC;
		goto error;
	}

	va_start(ap, oflags);
	mode = va_arg(ap, int);	/* unused */
	attr = va_arg(ap, struct mq_attr *);
	va_end(ap);

	err = mq_init(mq, attr);
	if (err)
		goto err_free_mq;

	xnlock_get_irqsave(&nklock, s);

	list_add_tail(&mq->link, &cobalt_mqq);

	err = -cobalt_node_add(&mq->nodebase, name, COBALT_MQ_MAGIC);
	if (err && err != -EEXIST)
		goto err_put_mq;

	if (err == -EEXIST) {
		err = -cobalt_node_get(&node, name, COBALT_MQ_MAGIC, oflags);
		if (err)
			goto err_put_mq;

		/* The same mq was created in the meantime, rollback. */
		xnlock_put_irqrestore(&nklock, s);
		mq_destroy(mq);
		xnfree(mq);
		mq = node2mq(node);
		goto got_mq;
	}

	xnlock_put_irqrestore(&nklock, s);

	/* Whether found or created, here we have a valid message queue. */
  got_mq:
	err = -cobalt_desc_create(&desc, &mq->nodebase,
				  oflags & (O_NONBLOCK | COBALT_PERMS_MASK));
	if (err)
		goto err_lock_put_mq;

	return (mqd_t) cobalt_desc_fd(desc);

  err_lock_put_mq:
	xnlock_get_irqsave(&nklock, s);
  err_put_mq:
	cobalt_node_put(&mq->nodebase);

	if (cobalt_node_removed_p(&mq->nodebase)) {
		/* mq is no longer referenced, we may destroy it. */

		xnlock_put_irqrestore(&nklock, s);
		mq_destroy(mq);
	  err_free_mq:
		xnfree(mq);
	} else
		xnlock_put_irqrestore(&nklock, s);
  error:
	return (mqd_t)err;
}

/**
 * Close a message queue.
 *
 * This service closes the message queue descriptor @a fd. The message queue is
 * destroyed only when all open descriptors are closed, and when unlinked with a
 * call to the mq_unlink() service.
 *
 * @param fd message queue descriptor.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EBADF, @a fd is an invalid message queue descriptor;
 * - EPERM, the caller context is invalid.
 *
 * @par Valid contexts:
 * - kernel module initialization or cleanup routine;
 * - kernel-space cancellation cleanup routine;
 * - user-space thread (Xenomai threads switch to secondary mode);
 * - user-space cancellation cleanup routine.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_close.html">
 * Specification.</a>
 *
 */
static inline int mq_close(mqd_t fd)
{
	cobalt_desc_t *desc;
	cobalt_mq_t *mq;
	spl_t s;
	int ret;

	xnlock_get_irqsave(&nklock, s);

	ret = -cobalt_desc_get(&desc, fd, COBALT_MQ_MAGIC);
	if (ret)
		goto out;

	mq = node2mq(cobalt_desc_node(desc));
	ret = -cobalt_node_put(&mq->nodebase);
	if (ret)
		goto out;

	if (mq->target_qd == fd)
		mq->target = NULL;

	if (cobalt_node_removed_p(&mq->nodebase)) {
		xnlock_put_irqrestore(&nklock, s);
		mq_destroy(mq);
		xnfree(mq);
	} else
		xnlock_put_irqrestore(&nklock, s);

	cobalt_desc_destroy(desc);

	return 0;
 out:
	xnlock_put_irqrestore(&nklock, s);

	return ret;
}

/**
 * Unlink a message queue.
 *
 * This service unlinks the message queue named @a name. The message queue is
 * not destroyed until all queue descriptors obtained with the mq_open() service
 * are closed with the mq_close() service. However, after a call to this
 * service, the unlinked queue may no longer be reached with the mq_open()
 * service.
 *
 * @param name name of the message queue to be unlinked.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - ENOENT, the message queue does not exist.
 *
 * @par Valid contexts:
 * - kernel module initialization or cleanup routine;
 * - kernel-space cancellation cleanup routine;
 * - user-space thread (Xenomai threads switch to secondary mode);
 * - user-space cancellation cleanup routine.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_unlink.html">
 * Specification.</a>
 *
 */
static inline int mq_unlink(const char *name)
{
	cobalt_node_t *node;
	cobalt_mq_t *mq;
	spl_t s;
	int err;

	xnlock_get_irqsave(&nklock, s);

	err = -cobalt_node_remove(&node, name, COBALT_MQ_MAGIC);
	if (!err && cobalt_node_removed_p(node)) {
		xnlock_put_irqrestore(&nklock, s);

		mq = node2mq(node);
		mq_destroy(mq);
		xnfree(mq);
	} else
		xnlock_put_irqrestore(&nklock, s);

	return err;
}

static inline
struct cobalt_msg *cobalt_mq_trysend(cobalt_mq_t **mqp,
				     cobalt_desc_t *desc, size_t len)
{
	struct cobalt_msg *msg;
	cobalt_mq_t *mq;
	unsigned flags;

	mq = node2mq(cobalt_desc_node(desc));
	flags = cobalt_desc_getflags(desc) & COBALT_PERMS_MASK;

	if (flags != O_WRONLY && flags != O_RDWR)
		return ERR_PTR(-EBADF);

	if (len > mq->attr.mq_msgsize)
		return ERR_PTR(-EMSGSIZE);

	msg = mq_msg_alloc(mq);
	if (msg == NULL)
		return ERR_PTR(-EAGAIN);

	if (list_empty(&mq->avail))
		xnselect_signal(&mq->write_select, 0);

	*mqp = mq;
	mq->nodebase.refcount++;

	return msg;
}

static inline struct cobalt_msg *cobalt_mq_tryrcv(cobalt_mq_t **mqp,
						  cobalt_desc_t *desc, size_t len)
{
	struct cobalt_msg *msg;
	unsigned int flags;
	cobalt_mq_t *mq;

	mq = node2mq(cobalt_desc_node(desc));
	flags = cobalt_desc_getflags(desc) & COBALT_PERMS_MASK;

	if (flags != O_RDONLY && flags != O_RDWR)
		return ERR_PTR(-EBADF);

	if (len < mq->attr.mq_msgsize)
		return ERR_PTR(-EMSGSIZE);

	*mqp = mq;

	if (list_empty(&mq->queued))
		return ERR_PTR(-EAGAIN);

	msg = list_get_entry(&mq->queued, struct cobalt_msg, link);
	mq->nrqueued--;

	if (list_empty(&mq->queued))
		xnselect_signal(&mq->read_select, 0);

	mq->nodebase.refcount++;

	return msg;
}

static struct cobalt_msg *
cobalt_mq_timedsend_inner(cobalt_mq_t **mqp, mqd_t fd,
			  size_t len, const struct timespec *abs_timeoutp)
{
	struct xnthread *cur = xnsched_current_thread();
	struct cobalt_msg *msg;
	cobalt_desc_t *desc;
	cobalt_mq_t *mq;
	xntmode_t tmode;
	xnticks_t to;
	spl_t s;
	int ret;

	xnlock_get_irqsave(&nklock, s);

	for (;;) {
		ret = cobalt_desc_get(&desc, fd, COBALT_MQ_MAGIC);
		if (ret) {
			msg = ERR_PTR(-ret);
			break;
		}

		msg = cobalt_mq_trysend(mqp, desc, len);
		if (msg != ERR_PTR(-EAGAIN))
			break;

		if ((cobalt_desc_getflags(desc) & O_NONBLOCK))
			break;

		to = XN_INFINITE;
		tmode = XN_RELATIVE;
		if (abs_timeoutp) {
			if ((unsigned long)abs_timeoutp->tv_nsec >= ONE_BILLION){
				msg = ERR_PTR(-EINVAL);
				break;
			}
			to = ts2ns(abs_timeoutp) + 1;
			tmode = XN_REALTIME;
		}

		mq = node2mq(cobalt_desc_node(desc));
		xnsynch_sleep_on(&mq->senders, to, tmode);

		if (xnthread_test_info(cur, XNBREAK)) {
			msg = ERR_PTR(-EINTR);
			break;
		}
		if (xnthread_test_info(cur, XNTIMEO)) {
			msg = ERR_PTR(-ETIMEDOUT);
			break;
		}
		if (xnthread_test_info(cur, XNRMID)) {
			msg = ERR_PTR(-EBADF);
			break;
		}
	}

	xnlock_put_irqrestore(&nklock, s);

	return msg;
}

static int
cobalt_mq_finish_send(mqd_t fd, cobalt_mq_t *mq, struct cobalt_msg *msg)
{
	struct cobalt_mqwait_context *mwc;
	struct xnthread_wait_context *wc;
	struct cobalt_sigpending *sigp;
	struct xnthread *thread;
	cobalt_desc_t *desc;
	int ret, removed;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	ret = -cobalt_desc_get(&desc, fd, COBALT_MQ_MAGIC);
	if (ret)
		goto bad_fd;

	if (node2mq(cobalt_desc_node(desc)) != mq) {
		ret = -EBADF;
		goto bad_fd;
	}

	/* Can we do pipelined sending? */
	if (xnsynch_pended_p(&mq->receivers)) {
		thread = xnsynch_wakeup_one_sleeper(&mq->receivers);
		wc = xnthread_get_wait_context(thread);
		mwc = container_of(wc, struct cobalt_mqwait_context, wc);
		mwc->msg = msg;
		xnthread_complete_wait(wc);
		mq->nodebase.refcount++;
	} else {
		/* Nope, have to go through the queue. */
		list_add_priff(msg, &mq->queued, prio, link);
		mq->nrqueued++;

		/*
		 * If first message and no pending reader, send a
		 * signal if notification was enabled via mq_notify().
		 */
		if (list_is_singular(&mq->queued)) {
			xnselect_signal(&mq->read_select, 1);
			if (mq->target) {
				sigp = cobalt_signal_alloc();
				if (sigp) {
					cobalt_copy_siginfo(SI_MESGQ, &sigp->si, &mq->si);
					cobalt_signal_send(mq->target, sigp, 0);
				}
				mq->target = NULL;
			}
		}
	}
unref:
	cobalt_node_put(&mq->nodebase);
	removed = cobalt_node_removed_p(&mq->nodebase);

	xnlock_put_irqrestore(&nklock, s);

	xnsched_run();

	if (removed) {
		mq_destroy(mq);
		xnfree(mq);
	}

	return ret;

bad_fd:
	/*
	 * descriptor was destroyed, simply return the message to the
	 * pool and wakeup any waiting sender.
	 */;
	mq_msg_free(mq, msg);

	if (list_is_singular(&mq->avail))
		xnselect_signal(&mq->write_select, 1);

	xnsynch_wakeup_one_sleeper(&mq->senders);

	goto unref;
}

static struct cobalt_msg *
cobalt_mq_timedrcv_inner(cobalt_mq_t **mqp, mqd_t fd,
			 size_t len, const struct timespec *abs_timeoutp)
{
	struct cobalt_mqwait_context mwc;
	struct cobalt_msg *msg;
	cobalt_desc_t *desc;
	cobalt_mq_t *mq;
	xntmode_t tmode;
	xnticks_t to;
	spl_t s;
	int ret;

	xnlock_get_irqsave(&nklock, s);

	ret = cobalt_desc_get(&desc, fd, COBALT_MQ_MAGIC);
	if (ret) {
		msg = ERR_PTR(-ret);
		goto out;
	}

	msg = cobalt_mq_tryrcv(mqp, desc, len);
	if (msg != ERR_PTR(-EAGAIN))
		goto out;

	if (cobalt_desc_getflags(desc) & O_NONBLOCK)
		goto out;

	to = XN_INFINITE;
	tmode = XN_RELATIVE;
	if (abs_timeoutp) {
		if (abs_timeoutp->tv_nsec >= ONE_BILLION) {
			msg = ERR_PTR(-EINVAL);
			goto out;
		}
		to = ts2ns(abs_timeoutp) + 1;
		tmode = XN_REALTIME;
	}

	mq = node2mq(cobalt_desc_node(desc));
	xnthread_prepare_wait(&mwc.wc);
	ret = xnsynch_sleep_on(&mq->receivers, to, tmode);
	if (ret == 0)
		msg = mwc.msg;
	else if (ret & XNRMID)
		msg = ERR_PTR(-EBADF);
	else if (ret & XNTIMEO)
		msg = ERR_PTR(-ETIMEDOUT);
	else
		msg = ERR_PTR(-EINTR);
out:
	xnlock_put_irqrestore(&nklock, s);

	return msg;
}

static int
cobalt_mq_finish_rcv(mqd_t fd, cobalt_mq_t *mq, struct cobalt_msg *msg)
{
	cobalt_desc_t *desc;
	int ret, removed;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	ret = -cobalt_desc_get(&desc, fd, COBALT_MQ_MAGIC);
	if (ret == 0 && node2mq(cobalt_desc_node(desc)) != mq)
		ret = -EBADF;

	mq_msg_free(mq, msg);

	if (list_is_singular(&mq->avail))
		xnselect_signal(&mq->write_select, 1);

	if (xnsynch_pended_p(&mq->senders))
		xnsynch_wakeup_one_sleeper(&mq->senders);

	cobalt_node_put(&mq->nodebase);
	removed = cobalt_node_removed_p(&mq->nodebase);

	xnlock_put_irqrestore(&nklock, s);

	xnsched_run();

	if (removed) {
		mq_destroy(mq);
		xnfree(mq);
	}

	return ret;
}

/**
 * Get the attributes object of a message queue.
 *
 * This service stores, at the address @a attr, the attributes of the messages
 * queue descriptor @a fd.
 *
 * The following attributes are set:
 * - @a mq_flags, flags of the message queue descriptor @a fd;
 * - @a mq_maxmsg, maximum number of messages in the message queue;
 * - @a mq_msgsize, maximum message size;
 * - @a mq_curmsgs, number of messages currently in the queue.
 *
 * @param fd message queue descriptor;
 *
 * @param attr address where the message queue attributes will be stored on
 * success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EBADF, @a fd is not a valid descriptor.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_getattr.html">
 * Specification.</a>
 *
 */
static inline int mq_getattr(mqd_t fd, struct mq_attr *attr)
{
	cobalt_desc_t *desc;
	cobalt_mq_t *mq;
	spl_t s;
	int err;

	xnlock_get_irqsave(&nklock, s);

	err = -cobalt_desc_get(&desc, fd, COBALT_MQ_MAGIC);
	if (err) {
		xnlock_put_irqrestore(&nklock, s);
		return err;
	}

	mq = node2mq(cobalt_desc_node(desc));
	*attr = mq->attr;
	attr->mq_flags = cobalt_desc_getflags(desc);
	attr->mq_curmsgs = mq->nrqueued;
	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/**
 * Set flags of a message queue.
 *
 * This service sets the flags of the @a fd descriptor to the value of the
 * member @a mq_flags of the @b mq_attr structure pointed to by @a attr.
 *
 * The previous value of the message queue attributes are stored at the address
 * @a oattr if it is not @a NULL.
 *
 * Only setting or clearing the O_NONBLOCK flag has an effect.
 *
 * @param fd message queue descriptor;
 *
 * @param attr pointer to new attributes (only @a mq_flags is used);
 *
 * @param oattr if not @a NULL, address where previous message queue attributes
 * will be stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EBADF, @a fd is not a valid message queue descriptor.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_setattr.html">
 * Specification.</a>
 *
 */
static inline int mq_setattr(mqd_t fd,
			     const struct mq_attr *__restrict__ attr,
			     struct mq_attr *__restrict__ oattr)
{
	cobalt_desc_t *desc;
	cobalt_mq_t *mq;
	long flags;
	spl_t s;
	int err;

	xnlock_get_irqsave(&nklock, s);

	err = -cobalt_desc_get(&desc, fd, COBALT_MQ_MAGIC);
	if (err) {
		xnlock_put_irqrestore(&nklock, s);
		return err;
	}

	mq = node2mq(cobalt_desc_node(desc));
	if (oattr) {
		*oattr = mq->attr;
		oattr->mq_flags = cobalt_desc_getflags(desc);
		oattr->mq_curmsgs = mq->nrqueued;
	}
	flags = (cobalt_desc_getflags(desc) & COBALT_PERMS_MASK)
	    | (attr->mq_flags & ~COBALT_PERMS_MASK);
	cobalt_desc_setflags(desc, flags);
	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/**
 * Register the current thread to be notified of message arrival at an empty
 * message queue.
 *
 * If @a evp is not @a NULL and is the address of a @b sigevent structure with
 * the @a sigev_notify member set to SIGEV_SIGNAL, the current thread will be
 * notified by a signal when a message is sent to the message queue @a fd, the
 * queue is empty, and no thread is blocked in call to mq_receive() or
 * mq_timedreceive(). After the notification, the thread is unregistered.
 *
 * If @a evp is @a NULL or the @a sigev_notify member is SIGEV_NONE, the current
 * thread is unregistered.
 *
 * Only one thread may be registered at a time.
 *
 * If the current thread is not a Cobalt thread (created with
 * pthread_create()), this service fails.
 *
 * Note that signals sent to user-space Cobalt threads will cause
 * them to switch to secondary mode.
 *
 * @param fd message queue descriptor;
 *
 * @param evp pointer to an event notification structure.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a evp is invalid;
 * - EPERM, the caller context is invalid;
 * - EBADF, @a fd is not a valid message queue descriptor;
 * - EBUSY, another thread is already registered.
 *
 * @par Valid contexts:
 * - Xenomai kernel-space Cobalt thread,
 * - Xenomai user-space Cobalt thread (switches to primary mode).
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_notify.html">
 * Specification.</a>
 *
 */
static inline int mq_notify(mqd_t fd, const struct sigevent *evp)
{
	struct cobalt_thread *thread = cobalt_current_thread();
	cobalt_desc_t *desc;
	cobalt_mq_t *mq;
	int err;
	spl_t s;

	if (evp && ((evp->sigev_notify != SIGEV_SIGNAL &&
		     evp->sigev_notify != SIGEV_NONE) ||
		    (unsigned int)(evp->sigev_signo - 1) > SIGRTMAX - 1))
		return -EINVAL;

	if (xnsched_interrupt_p() || thread == NULL)
		return -EPERM;

	xnlock_get_irqsave(&nklock, s);

	err = cobalt_desc_get(&desc, fd, COBALT_MQ_MAGIC);
	if (err)
		goto unlock_and_error;

	mq = node2mq(cobalt_desc_node(desc));
	if (mq->target && mq->target != thread) {
		err = -EBUSY;
		goto unlock_and_error;
	}

	if (evp == NULL || evp->sigev_notify == SIGEV_NONE)
		/* Here, mq->target == cobalt_current_thread() or NULL. */
		mq->target = NULL;
	else {
		mq->target = thread;
		mq->target_qd = fd;
		mq->si.si_signo = evp->sigev_signo;
		mq->si.si_errno = 0;
		mq->si.si_code = SI_MESGQ;
		mq->si.si_value = evp->sigev_value;
		/*
		 * XXX: we differ from the regular kernel here, which
		 * passes the sender's pid/uid data into the
		 * receiver's namespaces. We pass the receiver's creds
		 * into the init namespace instead.
		 */
		mq->si.si_pid = current->pid;
		mq->si.si_uid = current_uid();
	}

	xnlock_put_irqrestore(&nklock, s);
	return 0;

      unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
	return err;
}

int cobalt_mq_notify(mqd_t fd, const struct sigevent *__user evp)
{
	struct cobalt_process *cc;
	cobalt_assoc_t *assoc;
	struct sigevent sev;
	cobalt_ufd_t *ufd;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&cc->uqds, fd);
	if (assoc == NULL)
		return -EBADF;

	if (evp && __xn_safe_copy_from_user(&sev, evp, sizeof(sev)))
		return -EFAULT;

	ufd = assoc2ufd(assoc);

	return mq_notify(ufd->kfd, evp ? &sev : NULL);
}

int cobalt_mq_select_bind(mqd_t fd, struct xnselector *selector,
			  unsigned type, unsigned index)
{
	struct xnselect_binding *binding;
	cobalt_desc_t *desc;
	cobalt_mq_t *mq;
	int err;
	spl_t s;

	if (type == XNSELECT_READ || type == XNSELECT_WRITE) {
		binding = xnmalloc(sizeof(*binding));
		if (!binding)
			return -ENOMEM;
	} else
		return -EBADF;

	xnlock_get_irqsave(&nklock, s);
	err = -cobalt_desc_get(&desc, fd, COBALT_MQ_MAGIC);
	if (err)
		goto unlock_and_error;

	mq = node2mq(cobalt_desc_node(desc));

	switch(type) {
	case XNSELECT_READ:
		err = -EBADF;
		if ((cobalt_desc_getflags(desc) & COBALT_PERMS_MASK) == O_WRONLY)
			goto unlock_and_error;

		err = xnselect_bind(&mq->read_select, binding,
				    selector, type, index, !list_empty(&mq->queued));
		if (err)
			goto unlock_and_error;
		break;

	case XNSELECT_WRITE:
		err = -EBADF;
		if ((cobalt_desc_getflags(desc) & COBALT_PERMS_MASK) == O_RDONLY)
			goto unlock_and_error;

		err = xnselect_bind(&mq->write_select, binding,
				    selector, type, index, !list_empty(&mq->avail));
		if (err)
			goto unlock_and_error;
		break;
	}
	xnlock_put_irqrestore(&nklock, s);
	return 0;

      unlock_and_error:
	xnlock_put_irqrestore(&nklock, s);
	xnfree(binding);
	return err;
}

static void uqd_cleanup(cobalt_assoc_t *assoc)
{
	cobalt_ufd_t *ufd = assoc2ufd(assoc);
#if XENO_DEBUG(COBALT)
	printk(XENO_INFO "closing Cobalt mq descriptor 0x%lu\n",
	       cobalt_assoc_key(assoc));
#endif /* XENO_DEBUG(COBALT) */
	mq_close(ufd->kfd);
	xnfree(ufd);
}

void cobalt_mq_uqds_cleanup(struct cobalt_process *cc)
{
	cobalt_assocq_destroy(&cc->uqds, &uqd_cleanup);
}

/* mq_open(name, oflags, mode, attr, ufd) */
int cobalt_mq_open(const char __user *u_name, int oflags,
		   mode_t mode, struct mq_attr __user *u_attr, mqd_t uqd)
{
	struct mq_attr locattr, *attr;
	char name[COBALT_MAXNAME];
	struct cobalt_process *cc;
	cobalt_ufd_t *assoc;
	unsigned len;
	mqd_t kqd;
	int err;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return -EFAULT;

	if (len >= sizeof(name))
		return -ENAMETOOLONG;

	if (len == 0)
		return -EINVAL;

	if ((oflags & O_CREAT) && u_attr) {
		if (__xn_safe_copy_from_user(&locattr, u_attr, sizeof(locattr)))
			return -EFAULT;

		attr = &locattr;
	} else
		attr = NULL;

	kqd = mq_open(name, oflags, mode, attr);
	if ((long)kqd < 0)
		return (int)(long)kqd;

	assoc = xnmalloc(sizeof(*assoc));
	if (assoc == NULL) {
		mq_close(kqd);
		return -ENOSPC;
	}

	assoc->kfd = kqd;

	err = cobalt_assoc_insert(&cc->uqds, &assoc->assoc, (u_long)uqd);
	if (err) {
		xnfree(assoc);
		mq_close(kqd);
	}

	return err;
}

int cobalt_mq_close(mqd_t uqd)
{
	struct cobalt_process *cc;
	cobalt_assoc_t *assoc;
	int err;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	assoc = cobalt_assoc_remove(&cc->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	err = mq_close(assoc2ufd(assoc)->kfd);
	xnfree(assoc2ufd(assoc));

	return err;
}

int cobalt_mq_unlink(const char __user *u_name)
{
	char name[COBALT_MAXNAME];
	unsigned len;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return -EFAULT;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;

	return mq_unlink(name);
}

int cobalt_mq_getattr(mqd_t uqd, struct mq_attr __user *u_attr)
{
	struct cobalt_process *cc;
	cobalt_assoc_t *assoc;
	struct mq_attr attr;
	cobalt_ufd_t *ufd;
	int err;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&cc->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	err = mq_getattr(ufd->kfd, &attr);
	if (err)
		return err;

	return __xn_safe_copy_to_user(u_attr, &attr, sizeof(attr));
}

int cobalt_mq_setattr(mqd_t uqd, const struct mq_attr __user *u_attr,
		      struct mq_attr __user *u_oattr)
{
	struct mq_attr attr, oattr;
	struct cobalt_process *cc;
	cobalt_assoc_t *assoc;
	cobalt_ufd_t *ufd;
	int err;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&cc->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (__xn_safe_copy_from_user(&attr, u_attr, sizeof(attr)))
		return -EFAULT;

	err = mq_setattr(ufd->kfd, &attr, &oattr);
	if (err)
		return err;

	if (u_oattr)
		return __xn_safe_copy_to_user(u_oattr, &oattr, sizeof(oattr));

	return 0;
}

int cobalt_mq_send(mqd_t uqd, const void __user *u_buf, size_t len,
		   unsigned int prio)
{
	struct cobalt_process *cc;
	struct cobalt_msg *msg;
	cobalt_assoc_t *assoc;
	cobalt_ufd_t *ufd;
	cobalt_mq_t *mq;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&cc->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	if (prio >= COBALT_MSGPRIOMAX)
		return -EINVAL;

	if (len > 0 && !access_rok(u_buf, len))
		return -EFAULT;

	ufd = assoc2ufd(assoc);
	msg = cobalt_mq_timedsend_inner(&mq, ufd->kfd, len, NULL);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (__xn_copy_from_user(msg->data, u_buf, len)) {
		cobalt_mq_finish_send(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	msg->len = len;
	msg->prio = prio;

	return cobalt_mq_finish_send(ufd->kfd, mq, msg);
}

int cobalt_mq_timedsend(mqd_t uqd, const void __user *u_buf, size_t len,
			unsigned int prio, const struct timespec __user *u_ts)
{
	struct timespec timeout, *timeoutp;
	struct cobalt_process *cc;
	struct cobalt_msg *msg;
	cobalt_assoc_t *assoc;
	cobalt_ufd_t *ufd;
	cobalt_mq_t *mq;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&cc->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	if (prio >= COBALT_MSGPRIOMAX)
		return -EINVAL;

	if (len > 0 && !access_rok(u_buf, len))
		return -EFAULT;

	if (u_ts) {
		if (__xn_safe_copy_from_user(&timeout, u_ts, sizeof(timeout)))
			return -EFAULT;
		timeoutp = &timeout;
	} else
		timeoutp = NULL;

	ufd = assoc2ufd(assoc);
	msg = cobalt_mq_timedsend_inner(&mq, ufd->kfd, len, timeoutp);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if(__xn_copy_from_user(msg->data, u_buf, len)) {
		cobalt_mq_finish_send(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	msg->len = len;
	msg->prio = prio;

	return cobalt_mq_finish_send(ufd->kfd, mq, msg);
}

int cobalt_mq_receive(mqd_t uqd, void __user *u_buf,
		      ssize_t __user *u_len, unsigned int __user *u_prio)
{
	struct cobalt_process *cc;
	struct cobalt_msg *msg;
	cobalt_assoc_t *assoc;
	unsigned int prio;
	cobalt_ufd_t *ufd;
	cobalt_mq_t *mq;
	ssize_t len;
	int err;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&cc->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (__xn_safe_copy_from_user(&len, u_len, sizeof(len)))
		return -EFAULT;

	if (u_prio && !access_wok(u_prio, sizeof(prio)))
		return -EFAULT;

	if (len > 0 && !access_wok(u_buf, len))
		return -EFAULT;

	msg = cobalt_mq_timedrcv_inner(&mq, ufd->kfd, len, NULL);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (__xn_copy_to_user(u_buf, msg->data, msg->len)) {
		cobalt_mq_finish_rcv(ufd->kfd, mq, msg);
		return -EFAULT;
	}

	len = msg->len;
	prio = msg->prio;

	err = cobalt_mq_finish_rcv(ufd->kfd, mq, msg);
	if (err)
		return err;

	if (__xn_safe_copy_to_user(u_len, &len, sizeof(len)))
		return -EFAULT;

	if (u_prio &&
	    __xn_safe_copy_to_user(u_prio, &prio, sizeof(prio)))
		return -EFAULT;

	return 0;
}

int cobalt_mq_timedreceive(mqd_t uqd, void __user *u_buf,
			   ssize_t __user *u_len,
			   unsigned int __user *u_prio,
			   const struct timespec __user *u_ts)
{
	struct timespec timeout, *timeoutp;
	struct cobalt_process *cc;
	struct cobalt_msg *msg;
	cobalt_assoc_t *assoc;
	unsigned int prio;
	cobalt_ufd_t *ufd;
	cobalt_mq_t *mq;
	ssize_t len;
	int err;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	assoc = cobalt_assoc_lookup(&cc->uqds, (u_long)uqd);
	if (assoc == NULL)
		return -EBADF;

	ufd = assoc2ufd(assoc);

	if (__xn_safe_copy_from_user(&len, u_len, sizeof(len)))
		return -EFAULT;

	if (len > 0 && !access_wok(u_buf, len))
		return -EFAULT;

	if (u_ts) {
		if (__xn_safe_copy_from_user(&timeout, u_ts, sizeof(timeout)))
			return -EFAULT;

		timeoutp = &timeout;
	} else
		timeoutp = NULL;

	msg = cobalt_mq_timedrcv_inner(&mq, ufd->kfd, len, timeoutp);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (__xn_copy_to_user(u_buf, msg->data, msg->len)) {
		cobalt_mq_finish_rcv(ufd->kfd, mq, msg);
		return -EFAULT;
	}
	len = msg->len;
	prio = msg->prio;

	err = cobalt_mq_finish_rcv(ufd->kfd, mq, msg);
	if (err)
		return err;

	if (__xn_safe_copy_to_user(u_len, &len, sizeof(len)))
		return -EFAULT;

	if (u_prio && __xn_safe_copy_to_user(u_prio, &prio, sizeof(prio)))
		return -EFAULT;

	return 0;
}

int cobalt_mq_pkg_init(void)
{
	INIT_LIST_HEAD(&cobalt_mqq);

	return 0;
}

void cobalt_mq_pkg_cleanup(void)
{
	struct cobalt_mq *mq, *tmp;
	cobalt_node_t *node;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (list_empty(&cobalt_mqq))
		goto out;

	list_for_each_entry_safe(mq, tmp, &cobalt_mqq, link) {
		cobalt_node_remove(&node, mq->nodebase.name, COBALT_MQ_MAGIC);
		xnlock_put_irqrestore(&nklock, s);
		mq_destroy(mq);
#if XENO_DEBUG(COBALT)
		printk(XENO_INFO "unlinking Cobalt mq \"%s\"\n",
		       mq->nodebase.name);
#endif /* XENO_DEBUG(COBALT) */
		xnfree(mq);
		xnlock_get_irqsave(&nklock, s);
	}
out:
	xnlock_put_irqrestore(&nklock, s);
}

/*@}*/
