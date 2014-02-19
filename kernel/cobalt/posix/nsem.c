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
#include <cobalt/kernel/shadow.h>
#include <cobalt/kernel/tree.h>
#include "internal.h"
#include "sem.h"

DEFINE_XNLOCK(nsem_lock);

struct nsem {
	struct cobalt_sem *sem;
	struct cobalt_sem_shadow __user *usem;
	unsigned refs;
	struct xnid id;
};

static struct nsem *nsem_search(struct cobalt_process *cc, xnhandle_t handle)
{
	struct xnid *i;
	
	i = xnid_fetch(&cc->usems, handle);
	if (i == NULL)
		return NULL;
	
	return container_of(i, struct nsem, id);
}

static struct cobalt_sem_shadow __user *
nsem_open(struct cobalt_process *cc, struct cobalt_sem_shadow __user *ushadow, 
	const char *name, int oflags, mode_t mode, unsigned value)
{
	struct cobalt_sem_shadow shadow;
	struct cobalt_sem *sem;
	struct nsem *u, *v;
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

		xnlock_get_irqsave(&nsem_lock, s);
		u = nsem_search(cc, handle);
		if (u) {
			++u->refs;
			xnlock_put_irqrestore(&nsem_lock, s);
			return u->usem;
		}
		xnlock_put_irqrestore(&nsem_lock, s);

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
		sem = cobalt_sem_init_inner
			(&name[1], &shadow, SEM_PSHARED, value);
		if (IS_ERR(sem)) {
			rc = PTR_ERR(sem);
			if (rc == -EEXIST)
				goto retry_bind;
			return ERR_PTR(rc);
		}

		if (__xn_safe_copy_to_user(ushadow, &shadow, sizeof(shadow))) {
			cobalt_sem_destroy_inner(shadow.handle);
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

	xnlock_get_irqsave(&nsem_lock, s);
	v = nsem_search(cc, handle);
	if (v) {
		++v->refs;
		xnlock_put_irqrestore(&nsem_lock, s);
		xnlock_get_irqsave(&nklock, s);
		--sem->refs;
		xnlock_put_irqrestore(&nklock, s);

		xnfree(u);
		u = v;
	} else {
		xnid_enter(&cc->usems, &u->id, handle);
		xnlock_put_irqrestore(&nsem_lock, s);
	}

	return u->usem;
}

static int nsem_close(struct cobalt_process *cc, xnhandle_t handle)
{
	struct nsem *u;
	spl_t s;
	int err;

	xnlock_get_irqsave(&nsem_lock, s);
	u = nsem_search(cc, handle);
	if (u == NULL) {
		err = -ENOENT;
		goto err_unlock;
	}
	
	if (--u->refs) {
		err = 0;
		goto err_unlock;
	}
	
	xnid_remove(&cc->usems, &u->id);
	xnlock_put_irqrestore(&nsem_lock, s);
			
	cobalt_sem_destroy_inner(handle);
	
	xnfree(u);
	return 1;
	
  err_unlock:
	xnlock_put_irqrestore(&nsem_lock, s);
	return err;
}

void cobalt_nsem_unlink_inner(xnhandle_t handle)
{
	if (cobalt_sem_destroy_inner(handle) == -EBUSY)
		xnregistry_unlink(xnregistry_key(handle));
}

int cobalt_sem_open(struct cobalt_sem_shadow __user *__user *u_addr,
		const char __user *u_name,
		int oflags, mode_t mode, unsigned value)
{
	struct cobalt_sem_shadow __user *usm;
	struct cobalt_process *cc;
	char name[COBALT_MAXNAME + 1];
	long len;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	__xn_get_user(usm, u_addr);

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return len;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;
	if (len == 0)
		return -EINVAL;

	usm = nsem_open(cc, usm, name, oflags, mode, value);
	if (IS_ERR(usm))
		return PTR_ERR(usm);

	__xn_put_user(usm, u_addr);

	return 0;
}

int cobalt_sem_close(struct cobalt_sem_shadow __user *usm)
{
	struct cobalt_process *cc;
	xnhandle_t handle;

	cc = cobalt_process_context();
	if (cc == NULL)
		return -EPERM;

	handle = cobalt_get_handle_from_user(&usm->handle);

	return nsem_close(cc, handle);
}

int cobalt_sem_unlink(const char __user *u_name)
{
	char name[COBALT_MAXNAME + 1];
	xnhandle_t handle;
	long len;
	int rc;

	len = __xn_safe_strncpy_from_user(name, u_name, sizeof(name));
	if (len < 0)
		return len;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;

	if (name[0] != '/')
		return -EINVAL;

	rc = xnregistry_bind(&name[1], XN_NONBLOCK, XN_RELATIVE, &handle);
	if (rc == -EWOULDBLOCK)
		return -ENOENT;

	cobalt_nsem_unlink_inner(handle);
	return 0;
}

static void cobalt_usem_close(void *cookie, struct xnid *i)
{
	struct cobalt_process *cc;
	struct nsem *u;

	cc = cookie;
	u = container_of(i, struct nsem, id);
	u->refs = 1;
	nsem_close(cc, xnid_id(i));
}

void cobalt_sem_usems_cleanup(struct cobalt_process *cc)
{
	xntree_cleanup(&cc->usems, cc, cobalt_usem_close);
}
