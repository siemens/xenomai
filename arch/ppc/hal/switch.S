/*
 * Copyright (C) 2004 Philippe Gerum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
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

#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/page.h>
#include <asm/ppc_asm.h>
#include <xeno_config.h> 

#define RTHAL_SAVEREG(reg, pos)	stw reg,STACK_FRAME_OVERHEAD+4*(pos)(r1)
#define RTHAL_LOADREG(reg, pos)	lwz reg,STACK_FRAME_OVERHEAD+4*(pos)(r1)

/*
 * r3=out_kspp, r4=in_kspp
 */
_GLOBAL(rthal_switch_context)

        stwu    r1,-124(r1)

        /* Save general purpose registers. */

	RTHAL_SAVEREG(r31,0)
	RTHAL_SAVEREG(r30,1)
	RTHAL_SAVEREG(r29,2)
	RTHAL_SAVEREG(r28,3)
	RTHAL_SAVEREG(r27,4)
	RTHAL_SAVEREG(r26,5)
	RTHAL_SAVEREG(r25,6)
	RTHAL_SAVEREG(r24,7)
	RTHAL_SAVEREG(r23,8)
	RTHAL_SAVEREG(r22,9)
	RTHAL_SAVEREG(r21,10)
	RTHAL_SAVEREG(r20,11)
	RTHAL_SAVEREG(r19,12)
	RTHAL_SAVEREG(r18,13)
	RTHAL_SAVEREG(r17,14)
	RTHAL_SAVEREG(r16,15)
	RTHAL_SAVEREG(r15,16)
	RTHAL_SAVEREG(r14,17)
	RTHAL_SAVEREG(r13,18)
	RTHAL_SAVEREG(r3,19)
	RTHAL_SAVEREG(r2,20)
	RTHAL_SAVEREG(r0,21)

        /* Save special registers. */
	
	mfctr    r2
	RTHAL_SAVEREG(r2,22)
        mfcr     r2
	RTHAL_SAVEREG(r2,23)
        mfxer    r2
	RTHAL_SAVEREG(r2,24)
        mflr     r2
	RTHAL_SAVEREG(r2,25)
        mfmsr    r2
	RTHAL_SAVEREG(r2,26)

        /* Switch stacks. */
	
        stw      r1,0(r3)       /* *out_kspp = sp */
        lwz      r1,0(r4)       /* sp = *in_kspp */

        /* Restore special registers. */

	RTHAL_LOADREG(r2,26)
        mtmsr    r2
	RTHAL_LOADREG(r2,25)
        mtlr     r2
	RTHAL_LOADREG(r2,24)
        mtxer    r2
	RTHAL_LOADREG(r2,23)
        mtcr     r2
	RTHAL_LOADREG(r2,22)
        mtctr    r2

	/* Restore general purpose registers. */
	
	RTHAL_LOADREG(r0,21)
	RTHAL_LOADREG(r2,20)
	RTHAL_LOADREG(r3,19)
	RTHAL_LOADREG(r13,18)
	RTHAL_LOADREG(r14,17)
	RTHAL_LOADREG(r15,16)
	RTHAL_LOADREG(r16,15)
	RTHAL_LOADREG(r17,14)
	RTHAL_LOADREG(r18,13)
	RTHAL_LOADREG(r19,12)
	RTHAL_LOADREG(r20,11)
	RTHAL_LOADREG(r21,10)
	RTHAL_LOADREG(r22,9)
	RTHAL_LOADREG(r23,8)
	RTHAL_LOADREG(r24,7)
	RTHAL_LOADREG(r25,6)
	RTHAL_LOADREG(r26,5)
	RTHAL_LOADREG(r27,4)
	RTHAL_LOADREG(r28,3)
	RTHAL_LOADREG(r29,2)
	RTHAL_LOADREG(r30,1)
	RTHAL_LOADREG(r31,0)

        addi    r1,r1,124

        blr
