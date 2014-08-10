/*
 * This file is part of the Xenomai project.
 *
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _RTDM_UAPI_UDD_H
#define _RTDM_UAPI_UDD_H

struct udd_signotify {
	pid_t pid;
	int sig;
};

#define UDD_RTIOC_IRQEN		_IO(RTDM_CLASS_UDD, 0)
#define UDD_RTIOC_IRQDIS	_IO(RTDM_CLASS_UDD, 1)
#define UDD_RTIOC_IRQSIG	_IOW(RTDM_CLASS_UDD, 2, struct udd_signotify)

#endif /* !_RTDM_UAPI_UDD_H */
