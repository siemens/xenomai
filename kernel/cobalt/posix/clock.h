/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef _COBALT_POSIX_CLOCK_H
#define _COBALT_POSIX_CLOCK_H

#include <linux/time.h>
#include <cobalt/uapi/time.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int cobalt_clock_getres(clockid_t clock_id, struct timespec __user *u_ts);

int cobalt_clock_gettime(clockid_t clock_id, struct timespec __user *u_ts);

int cobalt_clock_settime(clockid_t clock_id, const struct timespec __user *u_ts);

int cobalt_clock_nanosleep(clockid_t clock_id, int flags,
			   const struct timespec __user *u_rqt,
			   struct timespec __user *u_rmt);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_COBALT_POSIX_CLOCK_H */
