/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _XENO_NUCLEUS_INTR_H
#define _XENO_NUCLEUS_INTR_H

#include <nucleus/types.h>

/* Possible return values of ISR. */
#define XN_ISR_NONE   	 0x1
#define XN_ISR_HANDLED	 0x2
/* Additional bits. */
#define XN_ISR_PROPAGATE 0x100
#define XN_ISR_NOENABLE  0x200
#define XN_ISR_BITMASK	 ~0xff

/* Creation flags. */
#define XN_ISR_SHARED	 0x1
#define XN_ISR_EDGE	 0x2

/* Operational flags. */
#define XN_ISR_ATTACHED	 0x10000

#if defined(__KERNEL__) || defined(__XENO_UVM__) || defined(__XENO_SIM__)

typedef struct xnintr {

#if defined(CONFIG_XENO_OPT_SHIRQ_LEVEL) || defined(CONFIG_XENO_OPT_SHIRQ_EDGE)
    struct xnintr *next; /* !< Next object in the IRQ-sharing chain. */
#endif /* CONFIG_XENO_OPT_SHIRQ_LEVEL || CONFIG_XENO_OPT_SHIRQ_EDGE */

    xnisr_t isr;	/* !< Interrupt service routine. */

    void *cookie;	/* !< User-defined cookie value. */

    unsigned long hits;	/* !< Number of receipts (since attachment). */

    xnflags_t flags; 	/* !< Creation flags. */

    unsigned irq;	/* !< IRQ number. */

    xniack_t iack;	/* !< Interrupt acknowledge routine. */

    const char *name;	/* !< Symbolic name. */

} xnintr_t;

extern xnintr_t nkclock;

#ifdef __cplusplus
extern "C" {
#endif

int xnintr_mount(void);

void xnintr_clock_handler(void);

int xnintr_irq_proc(unsigned int irq, char *str);

    /* Public interface. */

int xnintr_init(xnintr_t *intr,
		const char *name,
		unsigned irq,
		xnisr_t isr,
		xniack_t iack,
		xnflags_t flags);

int xnintr_destroy(xnintr_t *intr);

int xnintr_attach(xnintr_t *intr,
		  void *cookie);

int xnintr_detach(xnintr_t *intr);

int xnintr_enable(xnintr_t *intr);

int xnintr_disable(xnintr_t *intr);
    
xnarch_cpumask_t xnintr_affinity(xnintr_t *intr,
                                 xnarch_cpumask_t cpumask);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __XENO_UVM__ || __XENO_SIM__ */

#endif /* !_XENO_NUCLEUS_INTR_H */
