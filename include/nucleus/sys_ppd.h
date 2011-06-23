#ifndef _XENO_NUCLEUS_SYS_PPD_H
#define _XENO_NUCLEUS_SYS_PPD_H

#include <nucleus/ppd.h>
#include <nucleus/heap.h>

struct xnsys_ppd {
	xnshadow_ppd_t ppd;
	xnheap_t sem_heap;
#ifdef XNARCH_HAVE_MAYDAY
	unsigned long mayday_addr;
#endif
	xnarch_atomic_t refcnt;
#define ppd2sys(addr) container_of(addr, struct xnsys_ppd, ppd)
};

extern struct xnsys_ppd __xnsys_global_ppd;

#ifdef CONFIG_XENO_OPT_PERVASIVE

static inline struct xnsys_ppd *xnsys_ppd_get(int global)
{
	xnshadow_ppd_t *ppd;

	if (global || !(ppd = xnshadow_ppd_get(0)))
		return &__xnsys_global_ppd;

	return ppd2sys(ppd);
}

#else /* !CONFIG_XENO_OPT_PERVASIVE */

static inline struct xnsys_ppd *xnsys_ppd_get(int global)
{
	return &__xnsys_global_ppd;
}

#endif /* !CONFIG_XENO_OPT_PERVASIVE */

#endif /* _XENO_NUCLEUS_SYS_PPD_H */
