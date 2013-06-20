#ifndef _COBALT_KERNEL_PPD_H
#define _COBALT_KERNEL_PPD_H

#include <cobalt/kernel/shadow.h>
#include <cobalt/kernel/heap.h>

struct mm_struct;

struct xnshadow_ppd_key {
	unsigned long muxid;
	struct mm_struct *mm;
};

struct xnshadow_ppd {
	struct xnshadow_ppd_key key;
	struct xnholder link;
};

#define xnshadow_ppd_muxid(ppd) ((ppd)->key.muxid)
#define xnshadow_ppd_mm(ppd)    ((ppd)->key.mm)

/* Called with nklock locked irqs off. */
struct xnshadow_ppd *xnshadow_ppd_get(unsigned int muxid);

struct xnsys_ppd {
	struct xnshadow_ppd ppd;
	struct xnheap sem_heap;
	unsigned long mayday_addr;
	atomic_t refcnt;
	char *exe_path;
};

extern struct xnsys_ppd __xnsys_global_ppd;

static inline struct xnsys_ppd *xnsys_ppd_get(int global)
{
	struct xnshadow_ppd *ppd;

	if (global || (ppd = xnshadow_ppd_get(0)) == NULL)
		return &__xnsys_global_ppd;

	return container_of(ppd, struct xnsys_ppd, ppd);
}

#endif /* _COBALT_KERNEL_PPD_H */
