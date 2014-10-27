/*
 * Copyright (C) 2013 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
#include <cobalt/kernel/lock.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/tree.h>
#include "internal.h"
#include "sem.h"
#include "thread.h"
#include <trace/events/cobalt-posix.h>

DEFINE_PRIVATE_XNLOCK(named_sem_lock);

struct named_sem {
	struct cobalt_sem *sem;
	struct cobalt_sem_shadow __user *usem;
	unsigned int refs;
	struct xnid id;
	struct filename *filename;
};

static struct named_sem *sem_search(struct cobalt_process *cc, xnhandle_t handle)
{
	struct xnid *i;

	i = xnid_fetch(&cc->usems, handle);
	if (i == NULL)
		return NULL;

	return container_of(i, struct named_sem, id);
}

static struct cobalt_sem_shadow __user *
sem_open(struct cobalt_process *cc, struct cobalt_sem_shadow __user *ushadow,
	 struct filename *filename, int oflags, mode_t mode, unsigned int value)
{
	const char *name = filename->name;
	struct cobalt_sem_shadow shadow;
	struct named_sem *u, *v;
	struct cobalt_sem *sem;
	xnhandle_t handle;
	spl_t s;
	int rc;

	if (name[0] != '/' || name[1] == '\0')
		return ERR_PTR(-EINVAL);

  retry_bind:
	rc = xnregistry_bind(&name[1], XN_NONBLOCK, XN_RELATIVE, &handle);
	switch (rc) {
	case 0:
		/* Found */
		if ((oflags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
			return ERR_PTR(-EEXIST);

		xnlock_get_irqsave(&named_sem_lock, s);
		u = sem_search(cc, handle);
		if (u) {
			++u->refs;
			xnlock_put_irqrestore(&named_sem_lock, s);
			return u->usem;
		}
		xnlock_put_irqrestore(&named_sem_lock, s);

		xnlock_get_irqsave(&nklock, s);
		sem = xnregistry_lookup(handle, NULL);
		if (sem && sem->magic != COBALT_SEM_MAGIC) {
			xnlock_put_irqrestore(&nklock, s);
			return ERR_PTR(-EINVAL);
		}

		if (sem) {
			++sem->refs;
			xnlock_put_irqrestore(&nklock, s);
		} else {
			xnlock_put_irqrestore(&nklock, s);
			goto retry_bind;
		}
		break;

	case -EWOULDBLOCK:
		/* Not found */
		if ((oflags & O_CREAT) == 0)
			return ERR_PTR(-ENOENT);

		shadow.magic = 0;
		sem = __cobalt_sem_init(&name[1], &shadow,
					SEM_PSHARED | SEM_NAMED, value);
		if (IS_ERR(sem)) {
			rc = PTR_ERR(sem);
			if (rc == -EEXIST)
				goto retry_bind;
			return ERR_PTR(rc);
		}

		if (__xn_safe_copy_to_user(ushadow, &shadow, sizeof(shadow))) {
			__cobalt_sem_destroy(shadow.handle);
			return ERR_PTR(-EFAULT);
		}
		handle = shadow.handle;
		break;

	default:
		return ERR_PTR(rc);
	}

	u = xnmalloc(sizeof(*u));
	if (u == NULL)
		return ERR_PTR(-ENOMEM);

	u->sem = sem;
	u->usem = ushadow;
	u->refs = 1;
	u->filename = filename;

	xnlock_get_irqsave(&named_sem_lock, s);
	v = sem_search(cc, handle);
	if (v) {
		++v->refs;
		xnlock_put_irqrestore(&named_sem_lock, s);
		xnlock_get_irqsave(&nklock, s);
		--sem->refs;
		xnlock_put_irqrestore(&nklock, s);

		putname(filename);
		xnfree(u);
		u = v;
	} else {
		xnid_enter(&cc->usems, &u->id, handle);
		xnlock_put_irqrestore(&named_sem_lock, s);
	}

	trace_cobalt_psem_open(name, handle, oflags, mode, value);

	return u->usem;
}

static int sem_close(struct cobalt_process *cc, xnhandle_t handle)
{
	struct named_sem *u;
	spl_t s;
	int err;

	xnlock_get_irqsave(&named_sem_lock, s);
	u = sem_search(cc, handle);
	if (u == NULL) {
		err = -ENOENT;
		goto err_unlock;
	}

	if (--u->refs) {
		err = 0;
		goto err_unlock;
	}

	xnid_remove(&cc->usems, &u->id);
	xnlock_put_irqrestore(&named_sem_lock, s);

	__cobalt_sem_destroy(handle);

	putname(u->filename);
	xnfree(u);
	return 1;

  err_unlock:
	xnlock_put_irqrestore(&named_sem_lock, s);
	return err;
}

void __cobalt_sem_unlink(xnhandle_t handle)
{
	if (__cobalt_sem_destroy(handle) == -EBUSY)
		xnregistry_unlink(xnregistry_key(handle));
}

struct cobalt_sem_shadow __user *
__cobalt_sem_open(struct cobalt_sem_shadow __user *usm,
		  const char __user *u_name,
		  int oflags, mode_t mode, unsigned int value)
{
	struct filename *filename;
	struct cobalt_process *cc;

	cc = cobalt_current_process();
	if (cc == NULL)
		return ERR_PTR(-EPERM);

	filename = getname(u_name);
	if (IS_ERR(filename))
		return ERR_CAST(filename);

	usm = sem_open(cc, usm, filename, oflags, mode, value);
	if (IS_ERR(usm)) {
		trace_cobalt_psem_open_failed(filename->name, oflags, mode,
					      value, PTR_ERR(usm));
		putname(filename);
	}

	return usm;
}

COBALT_SYSCALL(sem_open, lostage,
	       int, (struct cobalt_sem_shadow __user *__user *u_addrp,
		     const char __user *u_name,
		     int oflags, mode_t mode, unsigned int value))
{
	struct cobalt_sem_shadow __user *usm;

	if (__xn_get_user(usm, u_addrp))
		return -EFAULT;

	usm = __cobalt_sem_open(usm, u_name, oflags, mode, value);
	if (IS_ERR(usm))
		return PTR_ERR(usm);

	return __xn_put_user(usm, u_addrp) ? -EFAULT : 0;
}

COBALT_SYSCALL(sem_close, lostage,
	       int, (struct cobalt_sem_shadow __user *usm))
{
	struct cobalt_process *cc;
	xnhandle_t handle;

	cc = cobalt_current_process();
	if (cc == NULL)
		return -EPERM;

	handle = cobalt_get_handle_from_user(&usm->handle);
	trace_cobalt_psem_close(handle);

	return sem_close(cc, handle);
}

static inline int sem_unlink(const char *name)
{
	xnhandle_t handle;
	int ret;

	if (name[0] != '/')
		return -EINVAL;

	ret = xnregistry_bind(name + 1, XN_NONBLOCK, XN_RELATIVE, &handle);
	if (ret == -EWOULDBLOCK)
		return -ENOENT;

	__cobalt_sem_unlink(handle);

	return 0;
}

COBALT_SYSCALL(sem_unlink, lostage,
	       int, (const char __user *u_name))
{
	struct filename *filename;
	int ret;

	filename = getname(u_name);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	trace_cobalt_psem_unlink(filename->name);
	ret = sem_unlink(filename->name);
	putname(filename);

	return ret;
}

static void cleanup_named_sems(void *cookie, struct xnid *i)
{
	struct cobalt_process *cc;
	struct named_sem *u;

	cc = cookie;
	u = container_of(i, struct named_sem, id);
	u->refs = 1;
	sem_close(cc, xnid_key(i));
}

void cobalt_sem_usems_cleanup(struct cobalt_process *cc)
{
	xntree_cleanup(&cc->usems, cc, cleanup_named_sems);
}
