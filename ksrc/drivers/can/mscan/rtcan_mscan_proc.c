/*
 * Copyright (C) 2006 Wolfgang Grandegger <wg@grandegger.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/delay.h>

#include <rtdm/rtdm_driver.h>

/* CAN device profile */
#include "rtcan_dev.h"
#include "rtcan_internal.h"
#include "rtcan_mscan_regs.h"

#define MSCAN_REG_ARGS(reg) \
	"%-8s 0x%02x\n", #reg, (int)(in_8(&regs->reg)) & 0xff

#ifdef CONFIG_XENO_DRIVERS_CAN_DEBUG

static int rtcan_mscan_proc_regs(char *buf, char **start, off_t offset,
				 int count, int *eof, void *data)
{
	struct rtcan_device *dev = (struct rtcan_device *)data;
	struct mscan_regs *regs = (struct mscan_regs *)dev->base_addr;
#ifdef MPC5xxx_GPIO
	struct mpc5xxx_gpio *gpio = (struct mpc5xxx_gpio *)MPC5xxx_GPIO;
	u32 port_config;
#endif
	u8 canctl0, canctl1;
	RTCAN_PROC_PRINT_VARS(80);

	if (!RTCAN_PROC_PRINT("MSCAN registers at %p\n", regs))
		goto done;
	canctl0 = in_8(&regs->canctl0);
	if (!RTCAN_PROC_PRINT("canctl0  0x%02x%s%s%s%s%s%s%s%s\n",
			      canctl0,
			      (canctl0 & MSCAN_RXFRM) ? " rxfrm" :"",
			      (canctl0 & MSCAN_RXACT) ? " rxact" :"",
			      (canctl0 & MSCAN_CSWAI) ? " cswai" :"",
			      (canctl0 & MSCAN_SYNCH) ? " synch" :"",
			      (canctl0 & MSCAN_TIME)  ? " time"  :"",
			      (canctl0 & MSCAN_WUPE)  ? " wupe"  :"",
			      (canctl0 & MSCAN_SLPRQ) ? " slprq" :"",
			      (canctl0 & MSCAN_INITRQ)? " initrq":"" ))
		goto done;
	canctl1 = in_8(&regs->canctl1);
	if (!RTCAN_PROC_PRINT("canctl1  0x%02x%s%s%s%s%s%s%s\n",
			       canctl1,
			      (canctl1 & MSCAN_CANE)  ? " cane"  :"",
			      (canctl1 & MSCAN_CLKSRC)? " clksrc":"",
			      (canctl1 & MSCAN_LOOPB) ? " loopb" :"",
			      (canctl1 & MSCAN_LISTEN)? " listen":"",
			      (canctl1 & MSCAN_WUPM)  ? " wump"  :"",
			      (canctl1 & MSCAN_SLPAK) ? " slpak" :"",
			      (canctl1 & MSCAN_INITAK)? " initak":""))
		goto done;
	if (!RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canbtr0 )) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canbtr1 )) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canrflg )) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canrier )) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(cantflg )) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(cantier )) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(cantarq )) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(cantaak )) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(cantbsel)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidac )) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canrxerr)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(cantxerr)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidar0)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidar1)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidar2)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidar3)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidmr0)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidmr1)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidmr2)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidmr3)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidar4)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidar5)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidar6)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidar7)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidmr4)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidmr5)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidmr6)) |
	    !RTCAN_PROC_PRINT(MSCAN_REG_ARGS(canidmr7)))
		goto done;

#ifdef MPC5xxx_GPIO
	if (!RTCAN_PROC_PRINT("GPIO registers\n"))
		goto done;
	port_config = in_be32(&gpio->port_config);
	if (!RTCAN_PROC_PRINT("port_config 0x%08x %s\n", port_config,
			      (port_config & 0x10000000 ?
			       "CAN1 on I2C1, CAN2 on TMR0/1 pins":
			       (port_config & 0x70) == 0x10 ?
			       "CAN1/2 on PSC2 pins": "MSCAN1/2 not routed")))
		goto done;
#endif

done:
	RTCAN_PROC_PRINT_DONE;
}

int rtcan_mscan_create_proc(struct rtcan_device* dev)
{
	struct proc_dir_entry *proc_entry;

	if (!dev->proc_root)
		return -EINVAL;

	proc_entry = create_proc_entry("registers", S_IFREG | S_IRUGO | S_IWUSR,
				       dev->proc_root);
	if (!proc_entry)
		goto error;
	proc_entry->read_proc = rtcan_mscan_proc_regs;
	proc_entry->data = dev;

	return 0;

error:
	printk("%s: unable to create /proc entries for MSCAN\n", dev->name);
	return -1;
}

void rtcan_mscan_remove_proc(struct rtcan_device* dev)
{
	if (!dev->proc_root)
		return;

	remove_proc_entry("registers", dev->proc_root);
}

#else /* !CONFIG_XENO_DRIVERS_CAN_DEBUG */

void rtcan_mscan_remove_proc(struct rtcan_device* dev)
{
}

int rtcan_mscan_create_proc(struct rtcan_device* dev)
{
	return 0;
}
#endif	/* CONFIG_XENO_DRIVERS_CAN_DEBUG */
