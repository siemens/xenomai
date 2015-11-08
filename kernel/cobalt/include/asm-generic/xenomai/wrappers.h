/*
 * Copyright (C) 2005-2012 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_ASM_GENERIC_WRAPPERS_H

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#error "Xenomai/cobalt requires Linux kernel 3.10 or above"
#endif

#ifdef CONFIG_IPIPE_LEGACY
#error "CONFIG_IPIPE_LEGACY must be switched off"
#endif

/*
 * To keep the #ifdefery as readable as possible, please:
 *
 * - keep the conditional structure flat, no nesting (e.g. do not nest
 *   the pre-3.11 conditions into the pre-3.14 ones).
 * - group all wrappers which share the same condition.
 * - identify the first kernel release for which the wrapper should
 *   be defined, instead of testing the existence of a preprocessor
 *   symbol, so that obsolete wrappers can be spotted.
 */

#define XENO_BACKPORT(__sym) xeno_backport_ ##__sym

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0)
#define DEVICE_ATTR_RW(_name)	__ATTR_RW(_name)
#define DEVICE_ATTR_RO(_name)	__ATTR_RO(_name)
#define DEVICE_ATTR_WO(_name)	__ATTR_WO(_name)
#endif /* < 3.11 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
#include <linux/dma-mapping.h>
#include <linux/hwmon.h>

#define dma_set_mask_and_coherent \
	XENO_BACKPORT(dma_set_mask_and_coherent)
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int rc = dma_set_mask(dev, mask);
	if (rc == 0)
		dma_set_coherent_mask(dev, mask);
	return rc;
}

#ifdef CONFIG_HWMON
#define hwmon_device_register_with_groups \
	XENO_BACKPORT(hwmon_device_register_with_groups)
struct device *
hwmon_device_register_with_groups(struct device *dev, const char *name,
				void *drvdata,
				const struct attribute_group **groups);

#define devm_hwmon_device_register_with_groups \
	XENO_BACKPORT(devm_hwmon_device_register_with_groups)
struct device *
devm_hwmon_device_register_with_groups(struct device *dev, const char *name,
				void *drvdata,
				const struct attribute_group **groups);
#endif /* hwmon */
#endif /* < 3,13 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#include <linux/pci.h>

#define get_current_uuid() current_uid()

#ifdef CONFIG_PCI
#define pci_enable_msix_range XENO_BACKPORT(pci_enable_msix_range)
#ifdef CONFIG_PCI_MSI
int pci_enable_msix_range(struct pci_dev *dev,
			struct msix_entry *entries,
			int minvec, int maxvec);
#else /* not pci msi */
static inline int pci_enable_msix_range(struct pci_dev *dev,
					struct msix_entry *entries,
					int minvec, int maxvec)
{ return -ENOSYS; }
#endif /* not pci msi */
#endif /* pci */
#else /* >= 3.14 */
#define get_current_uuid() from_kuid_munged(current_user_ns(), current_uid())
#endif /* >= 3.14 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
#define raw_cpu_ptr(v)	__this_cpu_ptr(v)
#endif /* < 3.15 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
#define smp_mb__before_atomic()  smp_mb()
#define smp_mb__after_atomic()   smp_mb()
#endif /* < 3.16 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0)
#include <linux/netdevice.h>

#undef alloc_netdev
#define alloc_netdev(sizeof_priv, name, name_assign_type, setup) \
	alloc_netdev_mqs(sizeof_priv, name, setup, 1, 1)
#endif /* < 3.17 */

#endif /* _COBALT_ASM_GENERIC_WRAPPERS_H */
