/*
 * Copyright (C) 2006-2010 Wolfgang Grandegger <wg@grandegger.com>
 *
 * Copyright (C) 2005, 2006 Sebastian Smolorz
 *                          <Sebastian.Smolorz@stud.uni-hannover.de>
 *
 * Derived from the PCAN project file driver/src/pcan_mpc5200.c:
 *
 * Copyright (c) 2003 Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * Copyright (c) 2005 Felix Daners, Plugit AG, felix.daners@plugit.ch
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
#include <rtdm/rtcan.h>
#include "rtcan_dev.h"
#include "rtcan_raw.h"
#include "rtcan_internal.h"
#include "rtcan_mscan_regs.h"
#include "rtcan_mscan.h"

#define RTCAN_MSCAN_DEVS  2

static char *mscan_ctlr_name  = "MSCAN-MPC5200";
static char *mscan_board_name = "unkown";

MODULE_AUTHOR("Wolfgang Grandegger <wg@grandegger.com>");
MODULE_DESCRIPTION("RT-Socket-CAN driver for MSCAN-MPC2500");
MODULE_SUPPORTED_DEVICE("MSCAN-MPC5200 CAN controller");
MODULE_LICENSE("GPL");

/** Module parameter for the CAN controllers' */

int port[RTCAN_MSCAN_DEVS] = {
#ifdef CONFIG_XENO_DRIVERS_CAN_MSCAN_1
#ifdef CONFIG_XENO_DRIVERS_CAN_MSCAN_2
	1, 1  /* Enable CAN 1 and 2 */
#else
	1, 0  /* Enable CAN 1 only  */
#endif
#else
#ifdef CONFIG_XENO_DRIVERS_CAN_MSCAN_2
	0, 1  /* Enable CAN 2 only  */
#else
#error "No CAN controller enabled, fix configuration!"
#endif
#endif
};
compat_module_param_array(port, int, RTCAN_MSCAN_DEVS, 0444);
MODULE_PARM_DESC(port, "Enabled CAN ports (1,1 or 0,1 or 0,1)");

/*
 * Note: on the MPC5200 the MSCAN clock source is the IP bus
 * clock (IP_CLK) while on the MPC5200B it is the oscillator
 * clock (SYS_XTAL_IN).
 */
unsigned int mscan_clock = CONFIG_XENO_DRIVERS_CAN_MSCAN_CLOCK;
module_param(mscan_clock, int, 0444);
MODULE_PARM_DESC(mscan_clock, "Clock frequency in Hz");

char *mscan_pins = NULL;
module_param(mscan_pins, charp, 0444);
MODULE_PARM_DESC(mscan_pins, "Routing to GPIO pins (PSC2 or I2C1/TMR01)");

static struct rtcan_device *rtcan_mscan_devs[RTCAN_MSCAN_DEVS];
static int rtcan_mscan_count;

static inline void __init mscan_gpio_config(void)
{
	struct mpc5xxx_gpio *gpio = (struct mpc5xxx_gpio *)MPC5xxx_GPIO;
	int can_to_psc2 = -1;
	u32 port_config;

#if defined(CONFIG_XENO_DRIVERS_CAN_MSCAN_ALT)
	can_to_psc2 = 0;
#elif defined(CONFIG_XENO_DRIVERS_CAN_MSCAN_PSC2)
	can_to_psc2 = 1;
#endif

	/* Configure CAN routing to GPIO pins.
	 */
	if (mscan_pins != NULL) {
		if (strncmp(mscan_pins, "psc2", 4) == 0 ||
		    !strncmp(mscan_pins, "PSC2", 4))
			can_to_psc2 = 1;
		else if (strncmp(mscan_pins, "i2c1/tmr01", 10) == 0 ||
			 strncmp(mscan_pins, "I2C1/TMR01", 10) == 0)
			can_to_psc2 = 0;
		else {
			printk("Module parameter mscan_pins=%s is invalid. "
			       "Please use PSC2 or I2C1/TMR01.\n", mscan_pins);
		}
	}

	if (!gpio || can_to_psc2 < 0) {
		printk("%s: use pre-configure CAN routing\n", RTCAN_DRV_NAME);
		return;
	}

	port_config = in_be32(&gpio->port_config);
	if (can_to_psc2) {
		port_config &= ~0x10000070;
		port_config |= 0x00000010;
		printk("%s: CAN 1 and 2 routed to PSC2 pins\n", RTCAN_DRV_NAME);
	} else {
		port_config |= 0x10000000;
		printk("%s: CAN 1 routed to I2C1 pins and CAN2 to TMR01 pins\n",
		       RTCAN_DRV_NAME);
	}
	out_be32(&gpio->port_config, port_config);
}

static inline int mscan_get_config(unsigned long *addr, unsigned int *irq)
{
#if defined(CONFIG_PPC_MERGE) || LINUX_VERSION_CODE > KERNEL_VERSION(2,6,27)
	/* Use Open Firmware device tree */
	struct device_node *np = NULL;
	unsigned int i;
	int ret;

	for (i = 0; i < RTCAN_MSCAN_DEVS; i++) {
		struct resource r[2] = {};

		np = of_find_compatible_node(np, NULL, "fsl,mpc5200-mscan");
		if (np == NULL)
			np = of_find_compatible_node(np, NULL, "mpc5200-mscan");
		if (np == NULL)
			break;
		ret = of_address_to_resource(np, 0, &r[0]);
		if (ret)
			return ret;
		of_irq_to_resource(np, 0, &r[1]);
		addr[i] = r[0].start;
		irq[i] = r[1].start;
		rtcan_mscan_count++;
	}
#else
	addr[0] = MSCAN_CAN1_ADDR;
	irq[0] = MSCAN_CAN1_IRQ;
	addr[1] = MSCAN_CAN2_ADDR;
	irq[1] = MSCAN_CAN2_IRQ;
	rtcan_mscan_count = 2;
#endif
	return 0;
}

static int __init rtcan_mscan_init_one(int idx, unsigned long addr, int irq)
{
	struct rtcan_device *dev;
	int ret;

	dev = rtcan_dev_alloc(0, 0);
	if (dev == NULL)
		return -ENOMEM;

	dev->base_addr = (unsigned long)ioremap(addr, MSCAN_SIZE);
	if (dev->base_addr == 0) {
		ret = -ENOMEM;
		printk("ERROR! ioremap of %#lx failed\n", addr);
		goto out_dev_free;
	}

	dev->ctrl_name = mscan_ctlr_name;
	dev->board_name = mscan_board_name;
	dev->can_sys_clock = mscan_clock;

	ret = rtcan_mscan_register(dev, irq, 1);
	if (ret)
		goto out_iounmap;

	/* Remember initialized devices */
	rtcan_mscan_devs[idx] = dev;

	printk("%s: %s driver: MSCAN port %d, base-addr 0x%lx, irq %d\n",
	       dev->name, RTCAN_DRV_NAME, idx + 1, addr, irq);

	return 0;

out_iounmap:
	iounmap((void *)dev->base_addr);

out_dev_free:
	rtcan_dev_free(dev);

	return ret;

}

static void rtcan_mscan_exit(void)
{
	int i;
	struct rtcan_device *dev;

	for (i = 0; i < rtcan_mscan_count; i++) {

		if ((dev = rtcan_mscan_devs[i]) == NULL)
			continue;

		printk("Unloading %s device %s\n", RTCAN_DRV_NAME, dev->name);

		rtcan_mscan_unregister(dev);
		iounmap((void *)dev->base_addr);
		rtcan_dev_free(dev);
	}

}

static int __init rtcan_mscan_init(void)
{
	int i, err;
	int unsigned long addr[RTCAN_MSCAN_DEVS];
	int irq[RTCAN_MSCAN_DEVS];

	if ((err = mscan_get_config(addr, irq)))
		return err;
	mscan_gpio_config();

	for (i = 0; i < rtcan_mscan_count; i++) {
		if (!port[i])
			continue;

		err = rtcan_mscan_init_one(i, addr[i], irq[i]);
		if (err) {
			rtcan_mscan_exit();
			return err;
		}
	}

	return 0;
}

module_init(rtcan_mscan_init);
module_exit(rtcan_mscan_exit);
