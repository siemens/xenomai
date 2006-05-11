/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <vxworks/defs.h>

#define ONE_BILLION 1000000000

static wind_tick_handler_t tick_handler;
static int tick_handler_arg;


void tickAnnounce(void)
{
    if(tick_handler != NULL)
        tick_handler(tick_handler_arg);

    xnpod_announce_tick(&nkclock);
}


static int __tickAnnounce(xnintr_t *intr)
{
    tickAnnounce();
    return XN_ISR_HANDLED | XN_ISR_NOENABLE;
}


int wind_sysclk_init(u_long init_rate)
{
    return sysClkRateSet(init_rate);
}


void wind_sysclk_cleanup(void)
{
    xnpod_reset_timer();	/* Back to the default timer setup. */
}


STATUS sysClkConnect (wind_tick_handler_t func, int arg)
{
    if(func == NULL)
        return ERROR;

    tick_handler_arg = arg;
    tick_handler = func;

    return OK;
}


void sysClkDisable (void)
{
    xnpod_stop_timer();
}


void sysClkEnable (void)
{
    /* Rely on the fact that even if sysClkDisable was called, the value of
       nkpod->tickvalue did not change. */
    xnpod_start_timer(xnpod_get_tickval(), &__tickAnnounce);
}


int sysClkRateGet (void)
{
    return xnpod_get_ticks2sec();
}


STATUS sysClkRateSet (int new_rate)
{
    int err;
    
    if(new_rate <= 0)
        return ERROR;

    if (testbits(nkpod->status,XNTIMED))
        xnpod_stop_timer();

    err = xnpod_start_timer(ONE_BILLION / new_rate , &__tickAnnounce);

    return err == 0 ? OK : ERROR;
}
