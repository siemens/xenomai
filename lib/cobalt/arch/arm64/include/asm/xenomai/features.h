/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _LIB_COBALT_ARM_FEATURES_H
#define _LIB_COBALT_ARM_FEATURES_H

#include_next <features.h>
#include <xeno_config.h>

#if defined(__ARM_ARCH_2__)
#define __LINUX_ARM_ARCH__ 2
#endif /* armv2 */

#if defined(__ARM_ARCH_3__)
#define __LINUX_ARM_ARCH__ 3
#endif /* armv3 */

#if defined(__ARM_ARCH_4__) || defined(__ARM_ARCH_4T__)
#define __LINUX_ARM_ARCH__ 4
#endif /* armv4 */

#if defined(__ARM_ARCH_5__) || defined(__ARM_ARCH_5T__) \
	|| defined(__ARM_ARCH_5E__) || defined(__ARM_ARCH_5TE__)
#define __LINUX_ARM_ARCH__ 5
#endif /* armv5 */

#if defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6K__) \
	|| defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__)
#define __LINUX_ARM_ARCH__ 6
#endif /* armv6 */

#if defined(__ARM_ARCH_7A__)
#define __LINUX_ARM_ARCH__ 7
#endif /* armv7 */

#if defined(__aarch64__)
#define __LINUX_ARM_ARCH__ 8
#endif /* armv8 */

#ifndef __LINUX_ARM_ARCH__
#error "Could not find current ARM architecture"
#endif

#if __LINUX_ARM_ARCH__ < 6 && defined(CONFIG_SMP)
#error "SMP not supported below armv6, compile with -march=armv6 or above"
#endif

#include <asm/xenomai/uapi/features.h>

int cobalt_fp_detect(void);

#endif /* !_LIB_COBALT_ARM_FEATURES_H */
