#ifndef _XENO_NUCLEUS_SYS_PPD_H
#define _XENO_NUCLEUS_SYS_PPD_H

#include <nucleus/ppd.h>
#include <nucleus/heap.h>

struct xnsys_ppd {
	xnshadow_ppd_t ppd;
	xnheap_t sem_heap;
	unsigned long mayday_addr;
	xnarch_atomic_t refcnt;
	char *exe_path;
#define ppd2sys(addr) container_of(addr, struct xnsys_ppd, ppd)
};

extern struct xnsys_ppd __xnsys_global_ppd;

static inline struct xnsys_ppd *xnsys_ppd_get(int global)
{
	xnshadow_ppd_t *ppd;

	if (global || (ppd = xnshadow_ppd_get(0)) == NULL)
		return &__xnsys_global_ppd;

	return ppd2sys(ppd);
}

#endif /* _XENO_NUCLEUS_SYS_PPD_H */
