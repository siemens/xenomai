/*
	drivers/net/tulip/pnic.c

	Maintained by Jeff Garzik <jgarzik@mandrakesoft.com>
	Copyright 2000,2001  The Linux Kernel Team
	Written/copyright 1994-2001 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	Please refer to Documentation/DocBook/tulip.{pdf,ps,html}
	for more information on this driver, or visit the project
	Web page at http://sourceforge.net/projects/tulip/

*/
/* Ported to RTnet by Wittawat Yamwong <wittawat@web.de> */

#include <linux/kernel.h>
#include "tulip.h"


void pnic_do_nway(/*RTnet*/struct rtnet_device *rtdev)
{
	struct tulip_private *tp = (struct tulip_private *)rtdev->priv;
	long ioaddr = rtdev->base_addr;
	u32 phy_reg = inl(ioaddr + 0xB8);
	u32 new_csr6 = tp->csr6 & ~0x40C40200;

	if (phy_reg & 0x78000000) { /* Ignore baseT4 */
		if (phy_reg & 0x20000000)		rtdev->if_port = 5;
		else if (phy_reg & 0x40000000)	rtdev->if_port = 3;
		else if (phy_reg & 0x10000000)	rtdev->if_port = 4;
		else if (phy_reg & 0x08000000)	rtdev->if_port = 0;
		tp->nwayset = 1;
		new_csr6 = (rtdev->if_port & 1) ? 0x01860000 : 0x00420000;
		outl(0x32 | (rtdev->if_port & 1), ioaddr + CSR12);
		if (rtdev->if_port & 1)
			outl(0x1F868, ioaddr + 0xB8);
		if (phy_reg & 0x30000000) {
			tp->full_duplex = 1;
			new_csr6 |= 0x00000200;
		}
		if (tulip_debug > 1)
			/*RTnet*/printk(KERN_DEBUG "%s: PNIC autonegotiated status %8.8x, %s.\n",
				   rtdev->name, phy_reg, medianame[rtdev->if_port]);
		if (tp->csr6 != new_csr6) {
			tp->csr6 = new_csr6;
			/* Restart Tx */
			tulip_restart_rxtx(tp);
		}
	}
}

#if 0
void pnic_lnk_change(/*RTnet*/struct rtnet_device *rtdev, int csr5)
{
	struct tulip_private *tp = (struct tulip_private *)rtdev->priv;
	long ioaddr = rtdev->base_addr;
	int phy_reg = inl(ioaddr + 0xB8);

	if (tulip_debug > 1)
		printk(KERN_DEBUG "%s: PNIC link changed state %8.8x, CSR5 %8.8x.\n",
			   rtdev->name, phy_reg, csr5);
	if (inl(ioaddr + CSR5) & TPLnkFail) {
		outl((inl(ioaddr + CSR7) & ~TPLnkFail) | TPLnkPass, ioaddr + CSR7);
		/* If we use an external MII, then we mustn't use the
		 * internal negotiation.
		 */
		if (tulip_media_cap[rtdev->if_port] & MediaIsMII)
			return;
		if (! tp->nwayset ) {
			tp->csr6 = 0x00420000 | (tp->csr6 & 0x0000fdff);
			outl(tp->csr6, ioaddr + CSR6);
			outl(0x30, ioaddr + CSR12);
			outl(0x0201F078, ioaddr + 0xB8); /* Turn on autonegotiation. */
		}
	} else if (inl(ioaddr + CSR5) & TPLnkPass) {
		if (tulip_media_cap[rtdev->if_port] & MediaIsMII) {
			/* RTnet: This code must never be executed concurrently to a
			   tp->lock holder in RT-context because tulip_check_duplex is
			   not RT-safe!*/
			//spin_lock(&tp->lock);
			tulip_check_duplex(rtdev);
			//spin_unlock(&tp->lock);
		} else {
			pnic_do_nway(rtdev);
		}
		outl((inl(ioaddr + CSR7) & ~TPLnkPass) | TPLnkFail, ioaddr + CSR7);
	}
}
#endif
