/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
 * Copyright (C) 2013,2014 Gilles Chanteperdrix <gch@xenomai.org>.
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
#include <linux/list.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <cobalt/kernel/registry.h>
#include <cobalt/kernel/lock.h>
#include <cobalt/kernel/ppd.h>
#include <trace/events/cobalt-rtdm.h>
#include <rtdm/fd.h>
#include "internal.h"

DEFINE_PRIVATE_XNLOCK(__rtdm_fd_lock);
static LIST_HEAD(rtdm_fd_cleanup_queue);
static struct semaphore rtdm_fd_cleanup_sem;

static int enosys(void)
{
	return -ENOSYS;
}

static int enodev(void)
{
	return -ENODEV;
}

static void nop_close(struct rtdm_fd *fd)
{
}

static inline struct rtdm_fd_index *
rtdm_fd_index_fetch(struct xnsys_ppd *p, int ufd)
{
	struct xnid *id = xnid_fetch(&p->fds, ufd);
	if (id == NULL)
		return NULL;

	return container_of(id, struct rtdm_fd_index, id);
}

static struct rtdm_fd *rtdm_fd_fetch(struct xnsys_ppd *p, int ufd)
{
	struct rtdm_fd_index *idx = rtdm_fd_index_fetch(p, ufd);
	if (idx == NULL)
		return NULL;

	return idx->fd;
}

#define assign_invalid_handler(__handler)				\
	do								\
		(__handler) = (typeof(__handler))enodev;		\
	while (0)

/* Calling this handler should beget ENODEV if not implemented. */
#define assign_invalid_default_handler(__handler)			\
	do								\
		if ((__handler) == NULL)				\
			(__handler) = (typeof(__handler))enodev;	\
	while (0)

#define __assign_default_handler(__handler, __placeholder)		\
	do								\
		if ((__handler) == NULL)				\
			(__handler) = (typeof(__handler))__placeholder;	\
	while (0)

/* Calling this handler should beget ENOSYS if not implemented. */
#define assign_default_handler(__handler)				\
	__assign_default_handler(__handler, enosys)

#define __rt(__handler)		__handler ## _rt
#define __nrt(__handler)	__handler ## _nrt

/*
 * Install a placeholder returning ENODEV if none of the dual handlers
 * are implemented, ENOSYS otherwise for NULL handlers to trigger the
 * adaptive switch.
 */
#define assign_default_dual_handlers(__handler)				\
	do								\
		if (__rt(__handler) || __nrt(__handler)) {		\
			assign_default_handler(__rt(__handler));	\
			assign_default_handler(__nrt(__handler));	\
		} else {						\
			assign_invalid_handler(__rt(__handler));	\
			assign_invalid_handler(__nrt(__handler));	\
		}							\
	while (0)

int rtdm_fd_enter(struct xnsys_ppd *p, struct rtdm_fd *fd, int ufd,
	unsigned int magic, struct rtdm_fd_ops *ops)
{
	struct rtdm_fd_index *idx;
	spl_t s;
	int err;

	secondary_mode_only();

	if (magic == XNFD_MAGIC_ANY) {
		err = -EINVAL;
		goto err;
	}

	idx = kmalloc(sizeof(*idx), GFP_KERNEL);
	if (idx == NULL) {
		err = -ENOMEM;
		goto err;
	}

	assign_default_dual_handlers(ops->ioctl);
	assign_default_dual_handlers(ops->read);
	assign_default_dual_handlers(ops->write);
	assign_default_dual_handlers(ops->recvmsg);
	assign_default_dual_handlers(ops->sendmsg);
	assign_invalid_default_handler(ops->select_bind);
	assign_invalid_default_handler(ops->mmap);
	__assign_default_handler(ops->close, nop_close);

	fd->magic = magic;
	fd->ops = ops;
	fd->cont = p;
	fd->refs = 1;

	idx->fd = fd;

	xnlock_get_irqsave(&__rtdm_fd_lock, s);
	err = xnid_enter(&p->fds, &idx->id, ufd);
	if (err < 0) {
		xnlock_put_irqrestore(&__rtdm_fd_lock, s);
		err = -EBUSY;
		goto err_free_index;
	}
	xnlock_put_irqrestore(&__rtdm_fd_lock, s);

	return 0;

  err_free_index:
	kfree(idx);
  err:
	if (ops->close)
		ops->close(fd);
	return err;
}

/**
 * @brief Retrieve and lock a RTDM file descriptor
 *
 * @param[in] ufd User-side file descriptor
 *
 * @return Pointer to the RTDM file descriptor matching @a ufd, or
 * ERR_PTR(-EBADF).
 *
 * @note The file descriptor returned must be later released by a call
 * to rtdm_fd_put().
 *
 * @coretags{unrestricted}
 */
struct rtdm_fd *rtdm_fd_get(struct xnsys_ppd *p, int ufd, unsigned int magic)
{
	struct rtdm_fd *res;
	spl_t s;

	xnlock_get_irqsave(&__rtdm_fd_lock, s);
	res = rtdm_fd_fetch(p, ufd);
	if (res == NULL || (magic != XNFD_MAGIC_ANY && res->magic != magic)) {
		res = ERR_PTR(-EBADF);
		goto err_unlock;
	}

	++res->refs;
  err_unlock:
	xnlock_put_irqrestore(&__rtdm_fd_lock, s);

	return res;
}
EXPORT_SYMBOL_GPL(rtdm_fd_get);

struct lostage_trigger_close {
	struct ipipe_work_header work; /* Must be first */
};

static void rtdm_fd_do_close(struct rtdm_fd *fd)
{
	secondary_mode_only();

	fd->ops->close(fd);

	if (!XENO_ASSERT(NUCLEUS, !spltest()))
		splnone();
}

static int rtdm_fd_cleanup_thread(void *data)
{
	struct rtdm_fd *fd;
	int err;
	spl_t s;

	for (;;) {
		set_cpus_allowed_ptr(current, cpu_online_mask);

		do {
			err = down_killable(&rtdm_fd_cleanup_sem);
		} while (err && !kthread_should_stop());

		if (kthread_should_stop())
			break;

		xnlock_get_irqsave(&__rtdm_fd_lock, s);
		fd = list_first_entry(&rtdm_fd_cleanup_queue,
				struct rtdm_fd, cleanup);
		list_del(&fd->cleanup);
		xnlock_put_irqrestore(&__rtdm_fd_lock, s);

		rtdm_fd_do_close(fd);
	}

	return 0;
}

static void lostage_trigger_close(struct ipipe_work_header *work)
{
	up(&rtdm_fd_cleanup_sem);
}

static void rtdm_fd_put_inner(struct rtdm_fd *fd, spl_t s)
{
	int destroy;

	destroy = --fd->refs == 0;
	xnlock_put_irqrestore(&__rtdm_fd_lock, s);

	if (!destroy)
		return;

	if (ipipe_root_p)
		rtdm_fd_do_close(fd);
	else {
		struct lostage_trigger_close closework = {
			.work = {
				.size = sizeof(closework),
				.handler = lostage_trigger_close,
			},
		};

		xnlock_get_irqsave(&__rtdm_fd_lock, s);
		list_add_tail(&fd->cleanup, &rtdm_fd_cleanup_queue);
		xnlock_put_irqrestore(&__rtdm_fd_lock, s);

		ipipe_post_work_root(&closework, work);
	}
}

/**
 * @brief Release a RTDM file descriptor obtained via rtdm_fd_get()
 *
 * @param[in] fd RTDM file descriptor to release
 *
 * @note Every call to rtdm_fd_get() must be matched by a call to
 * rtdm_fd_put().
 *
 * @coretags{unrestricted}
 */
void rtdm_fd_put(struct rtdm_fd *fd)
{
	spl_t s;

	xnlock_get_irqsave(&__rtdm_fd_lock, s);
	rtdm_fd_put_inner(fd, s);
}
EXPORT_SYMBOL_GPL(rtdm_fd_put);

/**
 * @brief Hold a reference on a RTDM file descriptor
 *
 * @param[in] fd Target file descriptor
 *
 * @note rtdm_fd_lock() increments the reference counter of @a fd. You
 * only need to call this function in special scenarios, e.g. when
 * keeping additional references to the file descriptor that have
 * different lifetimes. Only use rtdm_fd_lock() on descriptors that
 * are currently locked via an earlier rtdm_fd_get()/rtdm_fd_lock() or
 * while running a device operation handler.
 *
 * @coretags{unrestricted}
 */
int rtdm_fd_lock(struct rtdm_fd *fd)
{
	spl_t s;

	xnlock_get_irqsave(&__rtdm_fd_lock, s);
	if (fd->refs == 0) {
		xnlock_put_irqrestore(&__rtdm_fd_lock, s);
		return -EIDRM;
	}
	++fd->refs;
	xnlock_put_irqrestore(&__rtdm_fd_lock, s);

	return 0;
}
EXPORT_SYMBOL_GPL(rtdm_fd_lock);

/**
 * @brief Drop a reference on a RTDM file descriptor
 *
 * @param[in] fd Target file descriptor
 *
 * @note Every call to rtdm_fd_lock() must be matched by a call to
 * rtdm_fd_unlock().
 *
 * @coretags{unrestricted}
 */
void rtdm_fd_unlock(struct rtdm_fd *fd)
{
	spl_t s;

	xnlock_get_irqsave(&__rtdm_fd_lock, s);
	/* Warn if fd was unreferenced. */
	XENO_ASSERT(NUCLEUS, fd->refs > 0);
	rtdm_fd_put_inner(fd, s);
}
EXPORT_SYMBOL_GPL(rtdm_fd_unlock);

int rtdm_fd_ioctl(struct xnsys_ppd *p, int ufd, unsigned int request, ...)
{
	void __user *arg;
	struct rtdm_fd *fd;
	va_list args;
	int err;

	va_start(args, request);
	arg = va_arg(args, void __user *);
	va_end(args);

	fd = rtdm_fd_get(p, ufd, XNFD_MAGIC_ANY);
	if (IS_ERR(fd)) {
		err = PTR_ERR(fd);
		goto out;
	}

	trace_cobalt_fd_ioctl(current, fd, ufd, request);

	if (ipipe_root_p)
		err = fd->ops->ioctl_nrt(fd, request, arg);
	else
		err = fd->ops->ioctl_rt(fd, request, arg);

	if (!XENO_ASSERT(NUCLEUS, !spltest()))
		    splnone();

	if (err < 0) {
		int ret = __rt_dev_ioctl_fallback(fd, request, arg);
		if (ret != -ENOSYS)
			err = ret;
	}

	rtdm_fd_put(fd);
  out:
	if (err < 0)
		trace_cobalt_fd_ioctl_status(current, fd, ufd, err);

	return err;
}
EXPORT_SYMBOL_GPL(rtdm_fd_ioctl);

ssize_t
rtdm_fd_read(struct xnsys_ppd *p, int ufd, void __user *buf, size_t size)
{
	struct rtdm_fd *fd;
	ssize_t err;

	fd = rtdm_fd_get(p, ufd, XNFD_MAGIC_ANY);
	if (IS_ERR(fd)) {
		err = PTR_ERR(fd);
		goto out;
	}

	trace_cobalt_fd_read(current, fd, ufd, size);

	if (ipipe_root_p)
		err = fd->ops->read_nrt(fd, buf, size);
	else
		err = fd->ops->read_rt(fd, buf, size);

	if (!XENO_ASSERT(NUCLEUS, !spltest()))
		    splnone();

	rtdm_fd_put(fd);

  out:
	if (err < 0)
		trace_cobalt_fd_read_status(current, fd, ufd, err);

	return err;
}
EXPORT_SYMBOL_GPL(rtdm_fd_read);

ssize_t rtdm_fd_write(struct xnsys_ppd *p, int ufd,
		      const void __user *buf, size_t size)
{
	struct rtdm_fd *fd;
	ssize_t err;

	fd = rtdm_fd_get(p, ufd, XNFD_MAGIC_ANY);
	if (IS_ERR(fd)) {
		err = PTR_ERR(fd);
		goto out;
	}

	trace_cobalt_fd_write(current, fd, ufd, size);

	if (ipipe_root_p)
		err = fd->ops->write_nrt(fd, buf, size);
	else
		err = fd->ops->write_rt(fd, buf, size);

	if (!XENO_ASSERT(NUCLEUS, !spltest()))
		    splnone();

	rtdm_fd_put(fd);

  out:
	if (err < 0)
		trace_cobalt_fd_write_status(current, fd, ufd, err);

	return err;
}
EXPORT_SYMBOL_GPL(rtdm_fd_write);

ssize_t
rtdm_fd_recvmsg(struct xnsys_ppd *p, int ufd, struct msghdr *msg, int flags)
{
	struct rtdm_fd *fd;
	ssize_t err;

	fd = rtdm_fd_get(p, ufd, XNFD_MAGIC_ANY);
	if (IS_ERR(fd)) {
		err = PTR_ERR(fd);
		goto out;
	}

	trace_cobalt_fd_recvmsg(current, fd, ufd, flags);

	if (ipipe_root_p)
		err = fd->ops->recvmsg_nrt(fd, msg, flags);
	else
		err = fd->ops->recvmsg_rt(fd, msg, flags);

	if (!XENO_ASSERT(NUCLEUS, !spltest()))
		    splnone();

	rtdm_fd_put(fd);

  out:
	if (err < 0)
		trace_cobalt_fd_recvmsg_status(current, fd, ufd, err);

	return err;
}
EXPORT_SYMBOL_GPL(rtdm_fd_recvmsg);

ssize_t
rtdm_fd_sendmsg(struct xnsys_ppd *p, int ufd, const struct msghdr *msg, int flags)
{
	struct rtdm_fd *fd;
	ssize_t err;

	fd = rtdm_fd_get(p, ufd, XNFD_MAGIC_ANY);
	if (IS_ERR(fd)) {
		err = PTR_ERR(fd);
		goto out;
	}

	trace_cobalt_fd_sendmsg(current, fd, ufd, flags);

	if (ipipe_root_p)
		err = fd->ops->sendmsg_nrt(fd, msg, flags);
	else
		err = fd->ops->sendmsg_rt(fd, msg, flags);

	if (!XENO_ASSERT(NUCLEUS, !spltest()))
		    splnone();

	rtdm_fd_put(fd);

  out:
	if (err < 0)
		trace_cobalt_fd_sendmsg_status(current, fd, ufd, err);

	return err;
}
EXPORT_SYMBOL_GPL(rtdm_fd_sendmsg);

static void
rtdm_fd_close_inner(struct xnsys_ppd *p, struct rtdm_fd_index *idx, spl_t s)
{
	xnid_remove(&p->fds, &idx->id);
	rtdm_fd_put_inner(idx->fd, s);

	kfree(idx);
}

int rtdm_fd_close(struct xnsys_ppd *p, int ufd, unsigned int magic)
{
	struct rtdm_fd_index *idx;
	struct rtdm_fd *fd;
	spl_t s;

	xnlock_get_irqsave(&__rtdm_fd_lock, s);
	idx = rtdm_fd_index_fetch(p, ufd);
	if (idx == NULL)
		goto ebadf;

	fd = idx->fd;
	if (magic != XNFD_MAGIC_ANY && fd->magic != magic) {
	  ebadf:
		xnlock_put_irqrestore(&__rtdm_fd_lock, s);
		return -EBADF;
	}

	trace_cobalt_fd_close(current, fd, ufd, fd->refs);

	__rt_dev_unref(fd, xnid_id(&idx->id));
	rtdm_fd_close_inner(p, idx, s);

	return 0;
}
EXPORT_SYMBOL_GPL(rtdm_fd_close);

int rtdm_fd_mmap(struct xnsys_ppd *p, int ufd,
		 struct _rtdm_mmap_request *rma,
		 void * __user *u_addrp)
{
	struct rtdm_fd *fd;
	int ret;

	fd = rtdm_fd_get(p, ufd, XNFD_MAGIC_ANY);
	if (IS_ERR(fd)) {
		ret = PTR_ERR(fd);
		goto out;
	}

	trace_cobalt_fd_mmap(current, fd, ufd, rma);

	if (rma->flags & (MAP_FIXED|MAP_ANONYMOUS)) {
		ret = -ENODEV;
		goto unlock;
	}

	ret = __rtdm_mmap_from_fdop(fd, rma->length, rma->offset,
				    rma->prot, rma->flags, u_addrp);
unlock:
	rtdm_fd_put(fd);
out:
	if (ret)
		trace_cobalt_fd_mmap_status(current, fd, ufd, ret);

	return ret;
}

int rtdm_fd_valid_p(int ufd)
{
	struct rtdm_fd *fd;
	spl_t s;

	xnlock_get_irqsave(&__rtdm_fd_lock, s);
	fd = rtdm_fd_fetch(xnsys_ppd_get(0), ufd);
	xnlock_put_irqrestore(&__rtdm_fd_lock, s);

	return fd != NULL;
}

/**
 * @brief Bind a selector to specified event types of a given file descriptor
 * @internal
 *
 * This function is invoked by higher RTOS layers implementing select-like
 * services. It shall not be called directly by RTDM drivers.
 *
 * @param[in] fd File descriptor to bind to
 * @param[in,out] selector Selector object that shall be bound to the given
 * event
 * @param[in] type Event type the caller is interested in
 * @param[in] fd_index Index in the file descriptor set of the caller
 *
 * @return 0 on success, otherwise:
 *
 * - -EBADF is returned if the file descriptor @a fd cannot be resolved.
 *
 * - -EINVAL is returned if @a type or @a fd_index are invalid.
 *
 * @coretags{task-unrestricted}
 */
int rtdm_fd_select_bind(int ufd, struct xnselector *selector,
			unsigned int type)
{
	struct xnsys_ppd *p;
	struct rtdm_fd *fd;
	int rc;

	p = xnsys_ppd_get(0);
	fd = rtdm_fd_get(p, ufd, XNFD_MAGIC_ANY);
	if (IS_ERR(fd))
		return PTR_ERR(fd);

	rc = fd->ops->select_bind(fd, selector, type, ufd);

	if (!XENO_ASSERT(RTDM, !spltest()))
		    splnone();

	rtdm_fd_put(fd);

	return rc;
}

static void rtdm_fd_destroy(void *cookie, struct xnid *id)
{
	struct xnsys_ppd *p = cookie;
	struct rtdm_fd_index *idx;
	spl_t s;

	idx = container_of(id, struct rtdm_fd_index, id);
	xnlock_get_irqsave(&__rtdm_fd_lock, s);
	rtdm_fd_close_inner(p, idx, XNFD_MAGIC_ANY);
}

void rtdm_fd_cleanup(struct xnsys_ppd *p)
{
	xntree_cleanup(&p->fds, p, rtdm_fd_destroy);
}

void rtdm_fd_init(void)
{
	sema_init(&rtdm_fd_cleanup_sem, 0);
	kthread_run(rtdm_fd_cleanup_thread, NULL, "rtdm_fd");
}
