/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 * Copyright (c) 2019 Mitchell Horne <mhorne@FreeBSD.org>
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_CPU_H_
#define	_MACHINE_CPU_H_

#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <machine/frame.h>

#define	TRAPF_PC(tfp)		((tfp)->tf_ra)
#define	TRAPF_USERMODE(tfp)	(((tfp)->tf_sstatus & SSTATUS_SPP) == 0)

#define	cpu_getstack(td)	((td)->td_frame->tf_sp)
#define	cpu_setstack(td, sp)	((td)->td_frame->tf_sp = (sp))
#define	cpu_spinwait()		/* nothing */
#define	cpu_lock_delay()	DELAY(1)

#ifdef _KERNEL

/* The mvendorid CSR contains the JEDEC encoded vendor ID */
#define	MVENDORID_BANK_SHIFT	7
#define	MVENDORID_BANK(bank)	((bank) << MVENDORID_BANK_SHIFT)
#define	MVENDORID_OFFSET_MASK	0x7F
#define	MVENDORID_OFFSET(off)	((off) & MVENDORID_OFFSET_MASK)
#define	MVENDORID(bank, off)	(MVENDORID_BANK(bank) | MVENDORID_OFFSET(off))

#define	MVENDORID_UNKNOWN	MVENDORID(0, 0)
#define	MVENDORID_ANDES		MVENDORID(7, 30)
#define	MVENDORID_SIFIVE	MVENDORID(10, 9)

/* The marchid CSR contains */
#define	MARCHID_TYPE_SHIFT	(XLEN - 1)
#define	MARCHID_TYPE_OPEN	(0UL << MARCHID_TYPE_SHIFT)
#define	MARCHID_TYPE_CLOSED	(1UL << MARCHID_TYPE_SHIFT)
#define	MARCHID_ID_MASK		(~(1UL << MARCHID_TYPE_SHIFT))
#define	MARCHID_ID(id)		(id & MARCHID_ID_MASK)
#define	MARCHID(type, id)	(type | MARCHID_ID(id))

/* Open-Source RISC-V Architecture IDs */
#define	MARCHID_UNKNOWN		MARCHID(MARCHID_TYPE_OPEN, 0)
#define	MARCHID_ROCKET		MARCHID(MARCHID_TYPE_OPEN, 1)
#define	MARCHID_BOOM		MARCHID(MARCHID_TYPE_OPEN, 2)
#define	MARCHID_ARIANE		MARCHID(MARCHID_TYPE_OPEN, 3)
#define	MARCHID_RI5CY		MARCHID(MARCHID_TYPE_OPEN, 4)
#define	MARCHID_SPIKE		MARCHID(MARCHID_TYPE_OPEN, 5)
#define	MARCHID_ECLASS		MARCHID(MARCHID_TYPE_OPEN, 6)
#define	MARCHID_ORCA		MARCHID(MARCHID_TYPE_OPEN, 7)
#define	MARCHID_SCR1		MARCHID(MARCHID_TYPE_OPEN, 8)
#define	MARCHID_YARVI		MARCHID(MARCHID_TYPE_OPEN, 9)
#define	MARCHID_RVBS		MARCHID(MARCHID_TYPE_OPEN, 10)
#define	MARCHID_SWERV_EH1	MARCHID(MARCHID_TYPE_OPEN, 11)
#define	MARCHID_MSCC		MARCHID(MARCHID_TYPE_OPEN, 12)
#define	MARCHID_BLACKPARROT	MARCHID(MARCHID_TYPE_OPEN, 13)
#define	MARCHID_BSG_MANYCORE	MARCHID(MARCHID_TYPE_OPEN, 14)
#define	MARCHID_CCLASS		MARCHID(MARCHID_TYPE_OPEN, 15)

#define	MIMPID_UNKNOWN		0

extern char btext[];
extern char etext[];

void	cpu_halt(void) __dead2;
void	cpu_reset(void) __dead2;
void	fork_trampoline(void);
void	identify_cpu(void);
void	swi_vm(void *v);

static __inline uint64_t
get_cyclecount(void)
{

	return (rdcycle());
}

#endif

#endif /* !_MACHINE_CPU_H_ */
