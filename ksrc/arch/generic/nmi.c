/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for x86.
 *
 *   Copyright &copy; 2005 Gilles Chanteperdrix.
 *
 *   Xenomai is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation, Inc., 675 Mass Ave,
 *   Cambridge MA 02139, USA; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   Xenomai is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */

/**
 * @addtogroup hal
 *
 * Generic NMI watchdog services.
 *
 *@{*/

#include <linux/module.h>
#include <asm/xenomai/hal.h>

unsigned int rthal_maxlat_us;
EXPORT_SYMBOL_GPL(rthal_maxlat_tsc);

unsigned long rthal_maxlat_tsc;
EXPORT_SYMBOL_GPL(rthal_maxlat_us);

void rthal_nmi_set_maxlat(unsigned int maxlat_us)
{
	rthal_maxlat_us = maxlat_us;
	rthal_maxlat_tsc = rthal_llimd(maxlat_us * 1000ULL,
				       RTHAL_NMICLK_FREQ, 1000000000);
}

void __init rthal_nmi_init(void (*emergency) (struct pt_regs *))
{
	rthal_nmi_set_maxlat(CONFIG_XENO_HW_NMI_DEBUG_LATENCY_MAX);
	rthal_nmi_release();

	if (rthal_nmi_request(emergency))
		printk("Xenomai: NMI watchdog not available.\n");
	else
		printk("Xenomai: NMI watchdog started (threshold=%u us).\n",
		       rthal_maxlat_us);
}

/*@}*/
