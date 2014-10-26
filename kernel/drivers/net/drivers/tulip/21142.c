/*
	drivers/net/tulip/21142.c

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

#include "tulip.h"
#include <linux/pci.h>
#include <linux/delay.h>

u16 t21142_csr14[] = { 0xFFFF, 0x0705, 0x0705, 0x0000, 0x7F3D, };


void t21142_start_nway(/*RTnet*/struct rtnet_device *rtdev)
{
	struct tulip_private *tp = (struct tulip_private *)rtdev->priv;
	long ioaddr = rtdev->base_addr;
	int csr14 = ((tp->sym_advertise & 0x0780) << 9)  |
		((tp->sym_advertise & 0x0020) << 1) | 0xffbf;

	rtdev->if_port = 0;
	tp->nway = tp->mediasense = 1;
	tp->nwayset = tp->lpar = 0;
	if (tulip_debug > 1)
		printk(KERN_DEBUG "%s: Restarting 21143 autonegotiation, csr14=%8.8x.\n",
			   rtdev->name, csr14);
	outl(0x0001, ioaddr + CSR13);
	udelay(100);
	outl(csr14, ioaddr + CSR14);
	tp->csr6 = 0x82420000 | (tp->sym_advertise & 0x0040 ? FullDuplex : 0);
	outl(tp->csr6, ioaddr + CSR6);
	if (tp->mtable  &&  tp->mtable->csr15dir) {
		outl(tp->mtable->csr15dir, ioaddr + CSR15);
		outl(tp->mtable->csr15val, ioaddr + CSR15);
	} else
		outw(0x0008, ioaddr + CSR15);
	outl(0x1301, ioaddr + CSR12); 		/* Trigger NWAY. */
}


#if 0
void t21142_lnk_change(/*RTnet*/struct rtnet_device *rtdev, int csr5)
{
	struct tulip_private *tp = (struct tulip_private *)rtdev->priv;
	long ioaddr = rtdev->base_addr;
	int csr12 = inl(ioaddr + CSR12);

	if (tulip_debug > 1)
		printk(KERN_INFO"%s: 21143 link status interrupt %8.8x, CSR5 %x, "
			   "%8.8x.\n", rtdev->name, csr12, csr5, inl(ioaddr + CSR14));

	/* If NWay finished and we have a negotiated partner capability. */
	if (tp->nway  &&  !tp->nwayset  &&  (csr12 & 0x7000) == 0x5000) {
		int setup_done = 0;
		int negotiated = tp->sym_advertise & (csr12 >> 16);
		tp->lpar = csr12 >> 16;
		tp->nwayset = 1;
		if (negotiated & 0x0100)		rtdev->if_port = 5;
		else if (negotiated & 0x0080)	rtdev->if_port = 3;
		else if (negotiated & 0x0040)	rtdev->if_port = 4;
		else if (negotiated & 0x0020)	rtdev->if_port = 0;
		else {
			tp->nwayset = 0;
			if ((csr12 & 2) == 0  &&  (tp->sym_advertise & 0x0180))
				rtdev->if_port = 3;
		}
		tp->full_duplex = (tulip_media_cap[rtdev->if_port] & MediaAlwaysFD) ? 1:0;

		if (tulip_debug > 1) {
			if (tp->nwayset)
				printk(KERN_INFO "%s: Switching to %s based on link "
					   "negotiation %4.4x & %4.4x = %4.4x.\n",
					   rtdev->name, medianame[rtdev->if_port], tp->sym_advertise,
					   tp->lpar, negotiated);
			else
				printk(KERN_INFO "%s: Autonegotiation failed, using %s,"
					   " link beat status %4.4x.\n",
					   rtdev->name, medianame[rtdev->if_port], csr12);
		}

		if (tp->mtable) {
			int i;
			for (i = 0; i < tp->mtable->leafcount; i++)
				if (tp->mtable->mleaf[i].media == rtdev->if_port) {
					int startup = ! ((tp->chip_id == DC21143 && tp->revision == 65));
					tp->cur_index = i;
					tulip_select_media(rtdev, startup);
					setup_done = 1;
					break;
				}
		}
		if ( ! setup_done) {
			tp->csr6 = (rtdev->if_port & 1 ? 0x838E0000 : 0x82420000) | (tp->csr6 & 0x20ff);
			if (tp->full_duplex)
				tp->csr6 |= 0x0200;
			outl(1, ioaddr + CSR13);
		}
#if 0							/* Restart shouldn't be needed. */
		outl(tp->csr6 | RxOn, ioaddr + CSR6);
		if (tulip_debug > 2)
			printk(KERN_DEBUG "%s:  Restarting Tx and Rx, CSR5 is %8.8x.\n",
				   rtdev->name, inl(ioaddr + CSR5));
#endif
		tulip_start_rxtx(tp);
		if (tulip_debug > 2)
			printk(KERN_DEBUG "%s:  Setting CSR6 %8.8x/%x CSR12 %8.8x.\n",
				   rtdev->name, tp->csr6, inl(ioaddr + CSR6),
				   inl(ioaddr + CSR12));
	} else if ((tp->nwayset  &&  (csr5 & 0x08000000)
				&& (rtdev->if_port == 3  ||  rtdev->if_port == 5)
				&& (csr12 & 2) == 2) ||
			   (tp->nway && (csr5 & (TPLnkFail)))) {
		/* Link blew? Maybe restart NWay. */
		t21142_start_nway(rtdev);
	} else if (rtdev->if_port == 3  ||  rtdev->if_port == 5) {
		if (tulip_debug > 1)
			printk(KERN_INFO"%s: 21143 %s link beat %s.\n",
				   rtdev->name, medianame[rtdev->if_port],
				   (csr12 & 2) ? "failed" : "good");
		if ((csr12 & 2)  &&  ! tp->medialock) {
			t21142_start_nway(rtdev);
		} else if (rtdev->if_port == 5)
			outl(inl(ioaddr + CSR14) & ~0x080, ioaddr + CSR14);
	} else if (rtdev->if_port == 0  ||  rtdev->if_port == 4) {
		if ((csr12 & 4) == 0)
			printk(KERN_INFO"%s: 21143 10baseT link beat good.\n",
				   rtdev->name);
	} else if (!(csr12 & 4)) {		/* 10mbps link beat good. */
		if (tulip_debug)
			printk(KERN_INFO"%s: 21143 10mbps sensed media.\n",
				   rtdev->name);
		rtdev->if_port = 0;
	} else if (tp->nwayset) {
		if (tulip_debug)
			printk(KERN_INFO"%s: 21143 using NWay-set %s, csr6 %8.8x.\n",
				   rtdev->name, medianame[rtdev->if_port], tp->csr6);
	} else {		/* 100mbps link beat good. */
		if (tulip_debug)
			printk(KERN_INFO"%s: 21143 100baseTx sensed media.\n",
				   rtdev->name);
		rtdev->if_port = 3;
		tp->csr6 = 0x838E0000 | (tp->csr6 & 0x20ff);
		outl(0x0003FF7F, ioaddr + CSR14);
		outl(0x0301, ioaddr + CSR12);
		tulip_restart_rxtx(tp);
	}
}
#endif
