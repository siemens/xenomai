/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#include <errno.h>
#include <string.h>
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include "reference.h"
#include "internal.h"
#include "buffer.h"
#include "timer.h"

struct syncluster alchemy_buffer_table;

static struct alchemy_namegen buffer_namegen = {
	.prefix = "buffer",
	.length = sizeof ((struct alchemy_buffer *)0)->name,
};

DEFINE_SYNC_LOOKUP(buffer, RT_BUFFER);

static void buffer_finalize(struct syncobj *sobj)
{
	struct alchemy_buffer *bcb;
	bcb = container_of(sobj, struct alchemy_buffer, sobj);
	xnfree(bcb->buf);
	xnfree(bcb);
}
fnref_register(libalchemy, buffer_finalize);

int rt_buffer_create(RT_BUFFER *bf, const char *name,
		     size_t bufsz, int mode)
{
	struct alchemy_buffer *bcb;
	struct service svc;
	int sobj_flags = 0;
	int ret;

	if (threadobj_irq_p())
		return -EPERM;

	if (bufsz == 0)
		return -EINVAL;

	COPPERPLATE_PROTECT(svc);

	bcb = xnmalloc(sizeof(*bcb));
	if (bcb == NULL) {
		ret = __bt(-ENOMEM);
		goto fail;
	}

	bcb->buf = xnmalloc(bufsz);
	if (bcb == NULL) {
		ret = __bt(-ENOMEM);
		goto fail_bufalloc;
	}

	alchemy_build_name(bcb->name, name, &buffer_namegen);
	bcb->magic = buffer_magic;
	bcb->mode = mode;
	bcb->bufsz = bufsz;
	bcb->rdoff = 0;
	bcb->wroff = 0;
	bcb->fillsz = 0;
	if (mode & B_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	syncobj_init(&bcb->sobj, sobj_flags,
		     fnref_put(libalchemy, buffer_finalize));

	if (syncluster_addobj(&alchemy_buffer_table, bcb->name, &bcb->cobj)) {
		ret = -EEXIST;
		goto fail_register;
	}

	bf->handle = mainheap_ref(bcb, uintptr_t);

	COPPERPLATE_UNPROTECT(svc);

	return 0;

fail_register:
	syncobj_uninit(&bcb->sobj);
	xnfree(bcb->buf);
fail_bufalloc:
	xnfree(bcb);
fail:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_buffer_delete(RT_BUFFER *bf)
{
	struct alchemy_buffer *bcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	syncluster_delobj(&alchemy_buffer_table, &bcb->cobj);
	bcb->magic = ~buffer_magic;
	syncobj_destroy(&bcb->sobj, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

ssize_t rt_buffer_read_timed(RT_BUFFER *bf,
			     void *ptr, size_t size,
			     const struct timespec *abs_timeout)
{
	struct alchemy_buffer_wait *wait = NULL;
	struct alchemy_buffer *bcb;
	struct threadobj *thobj;
	size_t len, rbytes, n;
	struct syncstate syns;
	struct service svc;
	size_t rdoff;
	int ret = 0;
	void *p;

	len = size;
	if (len == 0)
		return 0;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	/*
	 * We may only return complete messages to readers, so there
	 * is no point in waiting for messages which are larger than
	 * what the buffer can hold.
	 */
	if (len > bcb->bufsz) {
		ret = -EINVAL;
		goto done;
	}
redo:
	for (;;) {
		/*
		 * We should be able to read a complete message of the
		 * requested length, or block.
		 */
		if (bcb->fillsz < len)
			goto wait;

		/* Read from the buffer in a circular way. */
		rdoff = bcb->rdoff;
		rbytes = len;
		p = ptr;

		do {
			if (rdoff + rbytes > bcb->bufsz)
				n = bcb->bufsz - rdoff;
			else
				n = rbytes;
			memcpy(p, bcb->buf + rdoff, n);
			p += n;
			rdoff = (rdoff + n) % bcb->bufsz;
			rbytes -= n;
		} while (rbytes > 0);

		bcb->fillsz -= len;
		bcb->rdoff = rdoff;
		ret = (ssize_t)len;

		/*
		 * Wake up all threads waiting for the buffer to
		 * drain, if we freed enough room for the leading one
		 * to post its message.
		 */
		thobj = syncobj_peek_drain(&bcb->sobj);
		if (thobj == NULL)
			goto done;

		wait = threadobj_get_wait(thobj);
		if (wait->size + bcb->fillsz <= bcb->bufsz)
			syncobj_drain(&bcb->sobj);

		goto done;
	wait:
		if (alchemy_poll_mode(abs_timeout)) {
			ret = -EWOULDBLOCK;
			goto done;
		}

		/*
		 * Check whether writers are already waiting for
		 * sending data, while we are about to wait for
		 * receiving some. In such a case, we have a
		 * pathological use of the buffer. We must allow for a
		 * short read to prevent a deadlock.
		 */
		if (bcb->fillsz > 0 && syncobj_count_drain(&bcb->sobj)) {
			len = bcb->fillsz;
			goto redo;
		}

		if (wait == NULL)
			wait = threadobj_prepare_wait(struct alchemy_buffer_wait);

		wait->size = len;

		ret = syncobj_wait_grant(&bcb->sobj, abs_timeout, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;
			break;
		}
	}
done:
	put_alchemy_buffer(bcb, &syns);
out:
	if (wait)
		threadobj_finish_wait();

	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

ssize_t rt_buffer_write_timed(RT_BUFFER *bf,
			      const void *ptr, size_t size,
			      const struct timespec *abs_timeout)
{
	struct alchemy_buffer_wait *wait = NULL;
	struct alchemy_buffer *bcb;
	struct threadobj *thobj;
	size_t len, rbytes, n;
	struct syncstate syns;
	struct service svc;
	const void *p;
	size_t wroff;
	int ret = 0;

	len = size;
	if (len == 0)
		return 0;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

	COPPERPLATE_PROTECT(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	/*
	 * We may only send complete messages, so there is no point in
	 * accepting messages which are larger than what the buffer
	 * can hold.
	 */
	if (len > bcb->bufsz) {
		ret = -EINVAL;
		goto done;
	}

	for (;;) {
		/*
		 * We should be able to write the entire message at
		 * once, or block.
		 */
		if (bcb->fillsz + len > bcb->bufsz)
			goto wait;

		/* Write to the buffer in a circular way. */
		wroff = bcb->wroff;
		rbytes = len;
		p = ptr;

		do {
			if (wroff + rbytes > bcb->bufsz)
				n = bcb->bufsz - wroff;
			else
				n = rbytes;

			memcpy(bcb->buf + wroff, p, n);
			p += n;
			wroff = (wroff + n) % bcb->bufsz;
			rbytes -= n;
		} while (rbytes > 0);

		bcb->fillsz += len;
		bcb->wroff = wroff;
		ret = (ssize_t)len;

		/*
		 * Wake up all threads waiting for input, if we
		 * accumulated enough data to feed the leading one.
		 */
		thobj = syncobj_peek_grant(&bcb->sobj);
		if (thobj == NULL)
			goto done;

		wait = threadobj_get_wait(thobj);
		if (wait->size <= bcb->fillsz)
			syncobj_grant_all(&bcb->sobj);

		goto done;
	wait:
		if (alchemy_poll_mode(abs_timeout)) {
			ret = -EWOULDBLOCK;
			goto done;
		}

		if (wait == NULL)
			wait = threadobj_prepare_wait(struct alchemy_buffer_wait);

		wait->size = len;

		/*
		 * Check whether readers are already waiting for
		 * receiving data, while we are about to wait for
		 * sending some. In such a case, we have the converse
		 * pathological use of the buffer. We must kick
		 * readers to allow for a short read to prevent a
		 * deadlock.
		 *
		 * XXX: instead of broadcasting a general wake up
		 * event, we could be smarter and wake up only the
		 * number of waiters required to consume the amount of
		 * data we want to send, but this does not seem worth
		 * the burden: this is an error condition, we just
		 * have to mitigate its effect, avoiding a deadlock.
		 */
		if (bcb->fillsz > 0 && syncobj_count_grant(&bcb->sobj))
			syncobj_grant_all(&bcb->sobj);

		ret = syncobj_wait_drain(&bcb->sobj, abs_timeout, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;
			break;
		}
	}
done:
	put_alchemy_buffer(bcb, &syns);
out:
	if (wait)
		threadobj_finish_wait();

	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_buffer_clear(RT_BUFFER *bf)
{
	struct alchemy_buffer *bcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	bcb->wroff = 0;
	bcb->rdoff = 0;
	bcb->fillsz = 0;
	syncobj_drain(&bcb->sobj);

	put_alchemy_buffer(bcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_buffer_inquire(RT_BUFFER *bf, RT_BUFFER_INFO *info)
{
	struct alchemy_buffer *bcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	COPPERPLATE_PROTECT(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	info->iwaiters = syncobj_count_grant(&bcb->sobj);
	info->owaiters = syncobj_count_drain(&bcb->sobj);
	info->totalmem = bcb->bufsz;
	info->availmem = bcb->bufsz - bcb->fillsz;
	strcpy(info->name, bcb->name);

	put_alchemy_buffer(bcb, &syns);
out:
	COPPERPLATE_UNPROTECT(svc);

	return ret;
}

int rt_buffer_bind(RT_BUFFER *bf,
		   const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_buffer_table,
				   timeout,
				   offsetof(struct alchemy_buffer, cobj),
				   &bf->handle);
}

int rt_buffer_unbind(RT_BUFFER *bf)
{
	bf->handle = 0;
	return 0;
}
