/**
 * @file
 * @note Copyright &copy; 2006 Gilles Chanteperdrix <gch@xenomai.org>
 * Per-process data.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
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
#ifndef _COBALT_KERNEL_PPD_H
#define _COBALT_KERNEL_PPD_H

#include <cobalt/kernel/list.h>
#include <cobalt/kernel/shadow.h>
#include <cobalt/kernel/lock.h>
#include <cobalt/kernel/heap.h>
#include <rtdm/fd.h>

#define NR_PERSONALITIES  4
#if BITS_PER_LONG < NR_PERSONALITIES
#error "NR_PERSONALITIES overflows internal bitmap"
#endif

void *xnshadow_get_context(unsigned int muxid);

struct xnsys_ppd {
	struct xnheap sem_heap;
	unsigned long mayday_addr;
	atomic_t refcnt;
	char *exe_path;
	struct rb_root fds;
};

struct xnshadow_process {
	struct mm_struct *mm;
	void *priv[NR_PERSONALITIES];
	struct hlist_node hlink;
	struct xnsys_ppd sys_ppd;
	unsigned long permap;
};

extern struct xnsys_ppd __xnsys_global_ppd;

static inline struct xnsys_ppd *xnsys_ppd_get(int global)
{
	struct xnshadow_process *ppd;

	if (global || (ppd = xnshadow_get_context(0)) == NULL)
		return &__xnsys_global_ppd;

	return &ppd->sys_ppd;
}

#endif /* _COBALT_KERNEL_PPD_H */
