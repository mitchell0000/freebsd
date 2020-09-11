/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2020 Mitchell Horne
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/signal.h>

#include <machine/frame.h>
#include <machine/gdb_machdep.h>
#include <machine/pcb.h>
#include <machine/riscvreg.h>

#include <gdb/gdb.h>

void *
gdb_cpu_getreg(int regnum, size_t *regsz)
{
	*regsz = gdb_cpu_regsz(regnum);
	
	if (kdb_thread == curthread) {
		switch (regnum) {
		case GDB_REG_RA: return (&kdb_frame->tf_ra);
		case 2: return (&kdb_frame->tf_sp);
		case 3: return (&kdb_frame->tf_gp);
		case 4: return (&kdb_frame->tf_tp);
		case 5: return (&kdb_frame->tf_t[0]);
		case 6: return (&kdb_frame->tf_t[1]);
		case 7: return (&kdb_frame->tf_t[2]);
		case 8: return (&kdb_frame->tf_s[0]); /* frame pointer */
		case 9: return (&kdb_frame->tf_s[1]);
		case 10: return (&kdb_frame->tf_a[0]);
		case 11: return (&kdb_frame->tf_a[1]);
		case GDB_REG_PC: return (&kdb_frame->tf_sepc);
		default:
			 return (NULL);
		}
	}
	switch (regnum) {
	case GDB_REG_PC: /* FALLTHROUGH */
	case GDB_REG_RA: return (&kdb_thrctx->pcb_ra);
	case 2: return (&kdb_thrctx->pcb_sp);
	case 3: return (&kdb_thrctx->pcb_gp);
	case 4: return (&kdb_thrctx->pcb_tp);
	}

	return (NULL);
}

void
gdb_cpu_setreg(int regnum, void *val)
{

	switch (regnum) {
	case GDB_REG_PC:
		kdb_thrctx->pcb_ra = *(register_t *)val;
		if (kdb_thread == curthread)
			kdb_frame->tf_sepc = *(register_t *)val;
	}
}

int
gdb_cpu_signal(int type, int code)
{

	switch (type) {
	case SCAUSE_BREAKPOINT:
		return (SIGTRAP);
		break;
	}

	return (SIGEMT);
}
