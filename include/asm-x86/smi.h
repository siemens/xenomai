/**
 *   Copyright &copy; 2005 Gilles Chanteperdrix.
 *
 *   SMI workaround for x86.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#ifndef _XENO_ASM_X86_MACHINE_H
#error "please don't include asm/smi.h directly"
#endif

#if defined(CONFIG_XENO_HW_SMI_DETECT) && defined(CONFIG_XENO_HW_SMI_WORKAROUND)
void mach_x86_smi_disable(void);
void mach_x86_smi_restore(void);
#else /* !CONFIG_XENO_HW_SMI_DETECT || !CONFIG_XENO_HW_SMI_WORKAROUND */
static inline void mach_x86_smi_disable(void) { }
static inline void mach_x86_smi_restore(void) { }
#endif /* !CONFIG_XENO_HW_SMI_DETECT || !CONFIG_XENO_HW_SMI_WORKAROUND */

#ifdef CONFIG_XENO_HW_SMI_DETECT
void mach_x86_smi_init(void);
#else /* !CONFIG_XENO_HW_SMI_DETECT */
static inline void mach_x86_smi_init(void) { }
#endif /* CONFIG_XENO_HW_SMI_DETECT */
