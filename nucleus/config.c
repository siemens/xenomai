/*
 * Copyright (C) 2002 Khalid Aziz <khalid_aziz@hp.com>
 * Copyright (C) 2002 Randy Dunlap <rddunlap@osdl.org>
 * Copyright (C) 2002 Al Stone <ahs3@fc.hp.com>
 * Copyright (C) 2002 Hewlett-Packard Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>

/*
 * Define xeno_config_data which contains the wrapped and compressed
 * configuration file.  We use the very same markers as the Linux
 * kernel ones in order to be able to reuse the extract-ikconfig
 * program for Xenomai. The config file has been compressed with
 * gzip and then bounded by two eight byte magic numbers to allow
 * extraction from a binary nucleus image:
 *
 *   IKCFG_ST
 *   <image>
 *   IKCFG_ED
 *
 * This portion of code has been lifted from Linux v2.6
 * kernel/configs.c.
 */

#define XENO_MAGIC_START	"IKCFG_ST"
#define XENO_MAGIC_END	"IKCFG_ED"
#include "config_data.h"

#define XENO_MAGIC_SIZE (sizeof(XENO_MAGIC_START) - 1)

int xeno_config_data_size = sizeof(xeno_config_data) - 1 - XENO_MAGIC_SIZE * 2;

int config_copy_data (char *buf, size_t len, loff_t pos)
{
    ssize_t count;

    if (pos >= xeno_config_data_size)
	return 0;

    count = min(len,(size_t)(xeno_config_data_size - pos));

    if (copy_to_user(buf,xeno_config_data + XENO_MAGIC_SIZE + pos,count))
	return -EFAULT;

    return count;
}
