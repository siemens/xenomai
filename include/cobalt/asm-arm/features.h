/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _COBALT_ASM_ARM_FEATURES_H
#define _COBALT_ASM_ARM_FEATURES_H

#define __XN_TSC_TYPE_NONE                  0
#define __XN_TSC_TYPE_KUSER                 1
#define __XN_TSC_TYPE_FREERUNNING           2
#define __XN_TSC_TYPE_DECREMENTER           3
#define __XN_TSC_TYPE_FREERUNNING_FAST_WRAP 4
#define __XN_TSC_TYPE_FREERUNNING_COUNTDOWN 5

#ifdef __KERNEL__

#if defined(CONFIG_CPU_SA1100) || defined(CONFIG_CPU_SA110)
#define CONFIG_XENO_ARM_SA1000	1
#endif

#ifdef CONFIG_AEABI
#define CONFIG_XENO_ARM_EABI 1
#endif

#ifndef CONFIG_IPIPE_ARM_KUSER_TSC
#error "I-pipe patch with kuser tsc required"
#endif

#else /* !__KERNEL__ */

#include <xeno_config.h>

#ifdef __ARM_EABI__
#define CONFIG_XENO_ARM_EABI 1
#endif

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

#ifndef __LINUX_ARM_ARCH__
#error "Could not find current ARM architecture"
#endif

#if __LINUX_ARM_ARCH__ < 6 && defined(CONFIG_SMP)
#error "SMP not supported below armv6, compile with -march=armv6 or above"
#endif

#endif /* !__KERNEL__ */

#include <asm-generic/xenomai/features.h>

/* The ABI revision level we use on this arch. */
#define XENOMAI_ABI_REV   4UL

#define XENOMAI_FEAT_DEP (__xn_feat_generic_mask)

#define XENOMAI_FEAT_MAN (__xn_feat_generic_man_mask)

static inline int check_abi_revision(unsigned long abirev)
{
    return abirev == XENOMAI_ABI_REV;
}

static inline const char *get_feature_label (unsigned feature)
{
    switch (feature) {
    default:
	    return get_generic_feature_label(feature);
    }
}

#define XNARCH_HAVE_LLMULSHFT    1
#define XNARCH_HAVE_NODIV_LLIMD  1

#endif /* !_COBALT_ASM_ARM_FEATURES_H */
