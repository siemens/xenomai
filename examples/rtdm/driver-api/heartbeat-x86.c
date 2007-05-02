/*
 * Simple RTDM demo that generates a running light on your PC keyboard.
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka-at-web.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rtaisec-reloaded; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <rtdm/rtdm_driver.h>

MODULE_LICENSE("GPL");

rtdm_task_t heartbeat_task;
int         end = 0;

#define HEARTBEAT_PERIOD    100000000   /* 100 ms */

void set_leds(int mask)
{
#ifdef CONFIG_X86
	while (inb(0x64) & 2);
	outb(0xED, 0x60);
	while (!(inb(0x64) & 1));
	inb(0x60);
	while (inb(0x64) & 2);
	outb(mask, 0x60);
	while (!(inb(0x64) & 1));
	inb(0x60);
#else
#warning Sorry, no lighty on x86 hardware :(
#endif
}

void heartbeat(void *cookie)
{
	int state = 0;
	int led_state[] = { 0x00, 0x01, 0x05, 0x07, 0x06, 0x02, 0x00 };


	while (!end) {
		rtdm_task_wait_period();
	
		set_leds(led_state[state++]);
	
		if (state > 6)
			state = 0;
	}
	set_leds(0);
}

int init_module(void)
{
	return rtdm_task_init(&heartbeat_task, "heartbeat", heartbeat, NULL,
			      99, HEARTBEAT_PERIOD);
}

void cleanup_module(void)
{
	end = 1;
	rtdm_task_join_nrt(&heartbeat_task, 100);
}
