/**
 *   @ingroup hal
 *   @file
 *
 *   SMI workaround for x86.
 *
 *   Cut/Pasted from Vitor Angelo "smi" module.
 *   Adapted by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/reboot.h>
#include <asm-generic/xenomai/pci_ids.h>
#include <asm/xenomai/hal.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define pci_get_class(c,f)	pci_find_class((c),(f))
#define pci_dev_put(d)		do { } while(0)
#endif

#define DEVFN		0xf8	/* device 31, function 0 */

#define PMBASE_B0	0x40
#define PMBASE_B1	0x41

#define SMI_CTRL_ADDR	0x30

static int rthal_smi;
module_param_named(smi, rthal_smi, int, 0400);
MODULE_PARM_DESC(smi, "SMI workaround: -1 disable, 0 detect only, 1 enable");

static unsigned rthal_smi_masked_bits = 1; /* Global disable bit */
module_param_named(smi_mask, rthal_smi_masked_bits, int, 0400);
MODULE_PARM_DESC(smi_mask, "Set of bits to mask in the SMI control register");

static unsigned rthal_smi_saved_bits;
static unsigned short rthal_smi_en_addr;

#define mask_bits(v, p) outl(inl(p)&~(v),(p))
#define set_bits(v, p)  outl(inl(p)|(v), (p))

static int rthal_smi_reboot(struct notifier_block *nb, ulong event, void *buf);

static struct notifier_block rthal_smi_notifier = {
	.notifier_call = rthal_smi_reboot
};

static int rthal_smi_reboot(struct notifier_block *nb, ulong event, void *buf)
{
	if (((event == SYS_RESTART) || (event == SYS_HALT) ||
	     (event == SYS_POWER_OFF)) && rthal_smi_en_addr)
		set_bits(rthal_smi_saved_bits, rthal_smi_en_addr);

	return NOTIFY_DONE;
}

void rthal_smi_disable(void)
{
	if (!rthal_smi_en_addr)
		return;

	rthal_smi_saved_bits = inl(rthal_smi_en_addr) & rthal_smi_masked_bits;
	mask_bits(rthal_smi_masked_bits, rthal_smi_en_addr);

	if (inl(rthal_smi_en_addr) & rthal_smi_masked_bits)
		printk("Xenomai: SMI workaround failed!\n");
	else
		printk("Xenomai: SMI workaround enabled\n");

	register_reboot_notifier(&rthal_smi_notifier);
}

void rthal_smi_restore(void)
{
	if (!rthal_smi_en_addr)
		return;

	printk("Xenomai: SMI configuration restored\n");

	set_bits(rthal_smi_saved_bits, rthal_smi_en_addr);

	unregister_reboot_notifier(&rthal_smi_notifier);
}

static unsigned short get_smi_en_addr(struct pci_dev *dev)
{
	u_int8_t byte0, byte1;

	pci_read_config_byte(dev, PMBASE_B0, &byte0);
	pci_read_config_byte(dev, PMBASE_B1, &byte1);
	return SMI_CTRL_ADDR + (((byte1 << 1) | (byte0 >> 7)) << 7);	// bits 7-15
}

void rthal_smi_init(void)
{
	struct pci_dev *dev = NULL;

	if (rthal_smi < 0)
		return;

	/*
	 * Do not use pci_register_driver, pci_enable_device, ...
	 * Just register the used ports.
	 */
	dev = pci_get_class(PCI_CLASS_BRIDGE_ISA << 8, NULL);

	if (dev == NULL || dev->bus->number || 
	    dev->devfn != DEVFN || dev->vendor != PCI_VENDOR_ID_INTEL) {
		pci_dev_put(dev);
		return;
	}

	if (rthal_smi == 0) {
		printk("Xenomai: SMI-enabled chipset found, but SMI "
		       "workaround disabled\n"
	     "         (see xeno_hal.smi parameter). You may encounter\n"
	     "         high interrupt latencies!\n");
		pci_dev_put(dev);
		return;
	}
	
	printk("Xenomai: SMI-enabled chipset found\n");
	rthal_smi_en_addr = get_smi_en_addr(dev);
	pci_dev_put(dev);
}

EXPORT_SYMBOL_GPL(rthal_smi_init);
EXPORT_SYMBOL_GPL(rthal_smi_disable);
EXPORT_SYMBOL_GPL(rthal_smi_restore);
