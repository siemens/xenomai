/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * Linux wrappers.
 */

#ifndef _XENO_ASM_GENERIC_WRAPPERS_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#if defined(CONFIG_XENO_OPT_HOSTRT) || defined(__IPIPE_FEATURE_REQUEST_TICKDEV)
#include <linux/ipipe_tickdev.h>
#endif /* CONFIG_XENO_OPT_HOSTRT || __IPIPE_FEATURE_REQUEST_TICKDEV */
#include <asm/io.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)

#include <linux/pid.h>

static inline struct task_struct *wrap_find_task_by_pid(pid_t nr)
{
	return pid_task(find_pid_ns(nr, &init_pid_ns), PIDTYPE_PID);
}

#else /* LINUX_VERSION_CODE < 2.6.31 */

#define wrap_find_task_by_pid(nr)	\
	find_task_by_pid_ns(nr, &init_pid_ns)

#endif /* LINUX_VERSION_CODE < 2.6.31 */

#define kill_proc(pid, sig, priv)	\
  kill_proc_info(sig, (priv) ? SEND_SIG_PRIV : SEND_SIG_NOINFO, pid)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
#define rthal_irq_descp(irq)	(irq_desc + (irq))
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32) */
#define rthal_irq_descp(irq)	irq_to_desc(irq)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
#define rthal_irqdesc_lock(irq, flags)					\
	spin_lock_irqsave(&rthal_irq_descp(irq)->lock, flags)
#define rthal_irqdesc_unlock(irq, flags)				\
	spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock, flags)
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33) */
#define rthal_irqdesc_lock(irq, flags)					\
	raw_spin_lock_irqsave(&rthal_irq_descp(irq)->lock, flags)
#define rthal_irqdesc_unlock(irq, flags)				\
	raw_spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock, flags)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33) */

/* FIXME: wrapping useless since 2.6.11 */
#define DECLARE_IOCTL_HANDLER(name, filp, cmd, arg)		\
	long name(struct file *filp, unsigned int cmd, unsigned long arg)

#ifndef DEFINE_SEMAPHORE
/* Legacy DECLARE_MUTEX vanished in 2.6.37 */
#define DEFINE_SEMAPHORE(sem) DECLARE_MUTEX(sem)
#endif

#include <linux/mm.h>
#ifndef pgprot_noncached
#define pgprot_noncached(p) (p)
#endif /* !pgprot_noncached */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37) && defined(CONFIG_GENERIC_HARDIRQS)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
#define irq_desc_get_chip(desc)	get_irq_desc_chip(desc)
#endif

/*
 * The irq chip descriptor has been heavily revamped in
 * 2.6.37. Provide generic accessors to the chip handlers we need for
 * kernels implementing those changes.
 */
#define rthal_irq_chip_enable(irq)					\
	({								\
		struct irq_desc *desc = rthal_irq_descp(irq);		\
		struct irq_chip *chip = irq_desc_get_chip(desc);	\
		int __ret__ = 0;					\
		if (unlikely(chip->irq_unmask == NULL))			\
			__ret__ = -ENODEV;				\
		else							\
			chip->irq_unmask(&desc->irq_data);		\
		__ret__;						\
	})
#define rthal_irq_chip_disable(irq)					\
	({								\
		struct irq_desc *desc = rthal_irq_descp(irq);		\
		struct irq_chip *chip = irq_desc_get_chip(desc);	\
		int __ret__ = 0;					\
		if (unlikely(chip->irq_mask == NULL))			\
			__ret__ = -ENODEV;				\
		else							\
			chip->irq_mask(&desc->irq_data);		\
		__ret__;						\
	})
#endif

#endif /* _XENO_ASM_GENERIC_WRAPPERS_H */
