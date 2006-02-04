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
#include <nucleus/queue.h>
#include <rtai/shm.h>


typedef struct xnshm_a {

	xnholder_t link;
	unsigned ref;
	void *shm;
	unsigned long name;
	int size;

} xnshm_a_t;

static inline xnshm_a_t *link2shma (xnholder_t *laddr)
{
	return laddr ? ((xnshm_a_t *)(((char *)laddr) - (int)(&((xnshm_a_t *)0)->link))) : 0;
}

xnqueue_t xnshm_allocq;


static xnshm_a_t* alloc_new_shm(unsigned long name, int size)
{
	xnshm_a_t *p;

	p = xnheap_alloc(&kheap,sizeof(xnshm_a_t));
	if (!p)
		return NULL;

	p->shm = xnheap_alloc(&kheap,size);
	if (!p->shm) {
		xnheap_free(&kheap,p);
		return NULL;
	}
	memset(p->shm, 0, size);

	inith(&p->link);
	p->ref = 1;
	p->name = name;
	p->size = size;

	return p;
}

/*
 * This version of rt_shm_alloc works only inter-intra kernel modules.
 * So far, there's no support for user-land Linux processes.
 * The suprt argument is not honored.
 * The shared memory is allocated from the Fusion kheap.
 */
void *rt_shm_alloc(unsigned long name, int size, int suprt)
{
	void *ret = NULL;
	xnholder_t *holder;
	xnshm_a_t *p;
	spl_t s;

	xnlock_get_irqsave(&nklock,s);

	holder = getheadq(&xnshm_allocq);

	while (holder != NULL) {
		p = link2shma(holder);

		if (p->name == name) {
			/* assert(size==p->size); */

			p->ref++;
			ret = p->shm;
			goto unlock_and_exit;
		}

		holder = nextq(&xnshm_allocq,holder);
	}

	p = alloc_new_shm(name, size);
	if (!p)
		goto unlock_and_exit;

	appendq(&xnshm_allocq, &p->link);

	ret = p->shm;

 unlock_and_exit:

	xnlock_put_irqrestore(&nklock,s);

	return ret;
}


int rt_shm_free(unsigned long name)
{
	int ret = 0;
	xnholder_t *holder;
	xnshm_a_t *p;
	spl_t s;

	xnlock_get_irqsave(&nklock,s);

	holder = getheadq(&xnshm_allocq);

	while (holder != NULL) {
		p = link2shma(holder);

		if (p->name == name && --p->ref == 0) {

			removeq(&xnshm_allocq, &p->link);
			ret = p->size;
			xnheap_free(&kheap,p->shm);
			xnheap_free(&kheap,p);
			break;
		}

		holder = nextq(&xnshm_allocq,holder);
	}

	xnlock_put_irqrestore(&nklock,s);

	return ret;
}


int __rtai_shm_pkg_init (void)

{
    initq(&xnshm_allocq);

    return 0;
}

void __rtai_shm_pkg_cleanup (void)

{
}


EXPORT_SYMBOL(rt_shm_alloc);
EXPORT_SYMBOL(rt_shm_free);
