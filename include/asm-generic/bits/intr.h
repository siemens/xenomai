/*
 * Copyright(C) 2001,2002,2003,2004,2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 *(at your option) any later version.
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

#ifndef _XENO_ASM_GENERIC_BITS_INTR_H
#define _XENO_ASM_GENERIC_BITS_INTR_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

static inline int xnarch_hook_irq(unsigned int irq,
				  ipipe_irq_handler_t handler,
				  ipipe_irq_ackfn_t ackfn,
				  void *cookie)
{
	return ipipe_request_irq(&rthal_archdata.domain,
				 irq, handler, cookie, ackfn);
}

static inline void xnarch_release_irq(unsigned int irq)
{
	ipipe_free_irq(&rthal_archdata.domain, irq);
}

static inline void xnarch_enable_irq(unsigned int irq)
{
	ipipe_enable_irq(irq);
}

static inline void xnarch_disable_irq(unsigned int irq)
{
	ipipe_disable_irq(irq);
}

static inline void xnarch_end_irq(unsigned int irq)
{
	ipipe_end_irq(irq);
}

static inline void xnarch_chain_irq(unsigned int irq)
{
	ipipe_post_irq_root(irq);
}

static inline void xnarch_set_irq_affinity(unsigned int irq,
					   xnarch_cpumask_t affinity)
{
#ifdef CONFIG_SMP
	ipipe_set_irq_affinity(irq, affinity);
#endif
}

static inline void *xnarch_get_irq_cookie(unsigned int irq)
{
	return __ipipe_irq_cookie(&rthal_archdata.domain, irq);
}

#endif /* !_XENO_ASM_GENERIC_BITS_INTR_H */
