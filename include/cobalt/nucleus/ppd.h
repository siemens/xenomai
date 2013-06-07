#ifndef _XENO_NUCLEUS_PPD_H
#define _XENO_NUCLEUS_PPD_H

#include <nucleus/queue.h>
#include <nucleus/shadow.h>

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

#endif /* _XENO_NUCLEUS_PPD_H */
