/*
 * Copyright (C) 2005-2007 Jan Kiszka <jan.kiszka@web.de>.
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>.
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

#ifndef _RTDM_INTERNAL_H
#define _RTDM_INTERNAL_H

#include <nucleus/ppd.h>

struct rtdm_process {
#ifdef CONFIG_PROC_FS
	char name[32];
        pid_t pid;
#endif /* CONFIG_PROC_FS */

	xnshadow_ppd_t ppd;
};

extern int __rtdm_muxid;

#endif /* _RTDM_INTERNAL_H */
