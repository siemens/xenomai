#ifndef _XENO_NUCLEUS_SYS_PPD_H
#define _XENO_NUCLEUS_SYS_PPD_H

#include <nucleus/ppd.h>
#include <nucleus/heap.h>

struct xnsys_ppd {
	struct xnshadow_ppd ppd;
	struct xnheap sem_heap;
	unsigned long mayday_addr;
	xnarch_atomic_t refcnt;
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

#endif /* _XENO_NUCLEUS_SYS_PPD_H */
