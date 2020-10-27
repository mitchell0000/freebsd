/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2020 The FreeBSD Foundation
 *
 * This software was developed by Mitchell Horne under sponsorship from the
 * FreeBSD Foundation.
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <machine/pcb.h>
#include <machine/sve.h>

static MALLOC_DEFINE(M_SVE_CTX, "sve_ctx", "Contexts for SVE state");

static u_int sve_max_vlen;

/*
 * Called from cpu_switch(), savectx()
 */
void
sve_state_save(struct thread *td, struct pcb *pcb)
{
	char *state;
	u_int vlen, plen;

	KASSERT(pcb != NULL, ("NULL SVE PCB"));

	state = pcb->sve_state;
	vlen = td->td_md.md_sve_vlen;
	plen = vlen / NBBY;

	KASSERT(state != NULL, ("%s called with NULL sve_state", __func__));
	KASSERT(vlen != 0, ("%s called with zero sve_vlen", __func__));

	/* Save the vector registers */
	sve_zreg_read(z0, 0, vlen, state);
	sve_zreg_read(z1, 1, vlen, state);
	sve_zreg_read(z2, 2, vlen, state);
	sve_zreg_read(z3, 3, vlen, state);
	sve_zreg_read(z4, 4, vlen, state);
	sve_zreg_read(z5, 5, vlen, state);
	sve_zreg_read(z6, 6, vlen, state);
	sve_zreg_read(z7, 7, vlen, state);
	sve_zreg_read(z8, 8, vlen, state);
	sve_zreg_read(z9, 9, vlen, state);
	sve_zreg_read(z10, 10, vlen, state);
	sve_zreg_read(z11, 10, vlen, state);
	sve_zreg_read(z12, 10, vlen, state);
	sve_zreg_read(z13, 10, vlen, state);
	sve_zreg_read(z14, 10, vlen, state);
	sve_zreg_read(z15, 10, vlen, state);
	sve_zreg_read(z16, 10, vlen, state);
	sve_zreg_read(z17, 10, vlen, state);
	sve_zreg_read(z18, 10, vlen, state);
	sve_zreg_read(z19, 10, vlen, state);
	sve_zreg_read(z20, 20, vlen, state);
	sve_zreg_read(z21, 21, vlen, state);
	sve_zreg_read(z22, 22, vlen, state);
	sve_zreg_read(z23, 23, vlen, state);
	sve_zreg_read(z24, 24, vlen, state);
	sve_zreg_read(z25, 25, vlen, state);
	sve_zreg_read(z26, 26, vlen, state);
	sve_zreg_read(z27, 27, vlen, state);
	sve_zreg_read(z28, 28, vlen, state);
	sve_zreg_read(z29, 29, vlen, state);
	sve_zreg_read(z30, 30, vlen, state);
	sve_zreg_read(z31, 31, vlen, state);

	/* Save the predicates */
	state += 32 * vlen;
	sve_pred_read(p0, 0, plen, state);
	sve_pred_read(p1, 1, plen, state);
	sve_pred_read(p2, 2, plen, state);
	sve_pred_read(p3, 3, plen, state);
	sve_pred_read(p4, 4, plen, state);
	sve_pred_read(p5, 5, plen, state);
	sve_pred_read(p6, 6, plen, state);
	sve_pred_read(p7, 7, plen, state);
	sve_pred_read(p8, 8, plen, state);
	sve_pred_read(p9, 9, plen, state);
	sve_pred_read(p10, 10, plen, state);
	sve_pred_read(p11, 11, plen, state);
	sve_pred_read(p12, 12, plen, state);
	sve_pred_read(p13, 13, plen, state);
	sve_pred_read(p14, 14, plen, state);
	sve_pred_read(p15, 15, plen, state);
	sve_pred_read(p16, 16, plen, state);
}

void
sve_state_restore(struct thread *td, struct pcb *pcb)
{
	char *state;
	u_int vlen, plen;

	vlen = td->sve_vlen;
	plen = vlen / 8;
	state = pcb->sve_state;

	KASSERT(vlen != 0, ("%s called with zero sve_vlen", __func__));
	KASSERT(state != NULL, ("%s called with NULL sve_state", __func__));

	critical_enter();

	sve_enable();

	/* Save the vector registers */
	sve_zreg_write(z0, 0, vlen, state);
	sve_zreg_write(z1, 1, vlen, state);
	sve_zreg_write(z2, 2, vlen, state);
	sve_zreg_write(z3, 3, vlen, state);
	sve_zreg_write(z4, 4, vlen, state);
	sve_zreg_write(z5, 5, vlen, state);
	sve_zreg_write(z6, 6, vlen, state);
	sve_zreg_write(z7, 7, vlen, state);
	sve_zreg_write(z8, 8, vlen, state);
	sve_zreg_write(z9, 9, vlen, state);
	sve_zreg_write(z10, 10, vlen, state);
	sve_zreg_write(z11, 10, vlen, state);
	sve_zreg_write(z12, 10, vlen, state);
	sve_zreg_write(z13, 10, vlen, state);
	sve_zreg_write(z14, 10, vlen, state);
	sve_zreg_write(z15, 10, vlen, state);
	sve_zreg_write(z16, 10, vlen, state);
	sve_zreg_write(z17, 10, vlen, state);
	sve_zreg_write(z18, 10, vlen, state);
	sve_zreg_write(z19, 10, vlen, state);
	sve_zreg_write(z20, 20, vlen, state);
	sve_zreg_write(z21, 21, vlen, state);
	sve_zreg_write(z22, 22, vlen, state);
	sve_zreg_write(z23, 23, vlen, state);
	sve_zreg_write(z24, 24, vlen, state);
	sve_zreg_write(z25, 25, vlen, state);
	sve_zreg_write(z26, 26, vlen, state);
	sve_zreg_write(z27, 27, vlen, state);
	sve_zreg_write(z28, 28, vlen, state);
	sve_zreg_write(z29, 29, vlen, state);
	sve_zreg_write(z30, 30, vlen, state);
	sve_zreg_write(z31, 31, vlen, state);

	/* Restore the predicates */
	state += 32 * vlen;
	sve_pred_write(p0, 0, plen, state);
	sve_pred_write(p1, 1, plen, state);
	sve_pred_write(p2, 2, plen, state);
	sve_pred_write(p3, 3, plen, state);
	sve_pred_write(p4, 4, plen, state);
	sve_pred_write(p5, 5, plen, state);
	sve_pred_write(p6, 6, plen, state);
	sve_pred_write(p7, 7, plen, state);
	sve_pred_write(p8, 8, plen, state);
	sve_pred_write(p9, 9, plen, state);
	sve_pred_write(p10, 10, plen, state);
	sve_pred_write(p11, 11, plen, state);
	sve_pred_write(p12, 12, plen, state);
	sve_pred_write(p13, 13, plen, state);
	sve_pred_write(p14, 14, plen, state);
	sve_pred_write(p15, 15, plen, state);
	sve_pred_write(p16, 16, plen, state);

	critical_exit();
}

static void *
sve_state_alloc_ctx(u_int vlen)
{

	/*
	 * Calculate the size. We have 32 vector registers of size vlen, and 16
	 * predicate registers of size vlen / 8.
	 */
	KASSERT((vlen & 0x7f) == 0, ("%s: invalid vlen", __func__));
	size = NUM_ZREGS * vlen + NUM_PREGS * (vlen / NBBY);

	return (malloc(size, M_SVE_CTX, M_WAITOK));
}

static void
sve_state_free_ctx(void *ctx)
{

	free(ctx, M_SVE_CTX);
}

void *
sve_state_duplicate(struct *pcb, struct thread *td)
{
	void *res;

	if (pcb->pcb_svestate == NULL || td->td_md.md_sve_vlen == 0)
		return (NULL);

	res = sve_state_alloc_ctx(td->td_md.md_sve_vlen);
	bcopy(pcb->pcb_svestate, res, size);

	return (res);
}

void
sve_enable(void)
{
	uint64_t cpacr, zcr;

	/* Disable SVE traps */
	cpacr = READ_SPECIALREG(cpacr_el1);
	cpacr |= CPACR_SVE_TRAP_NONE;
	WRITE_SPECIALREG(cpacr_elf1, cpacr);

	/* Set the VLEN */
	zcr = READ_SPECIALREG(cpacr_el1);

}

void
sve_disable(void)
{
	uint64_t cpacr;

	cpacr = READ_SPECIALREG(cpacr_el1);
	cpacr &= ~CPACR_SVE_MASK;
	WRITE_SPECIALREG(cpacr_el1, cpacr);
}

void
sve_init(void *arg __unused)
{
	if (HWCAP_

	/* Write to ZCR.LEN and read back to determine our largest supported VLEN */

	sve_disable();
}
SYSINIT(sve, SI_SUB_CPU, SI_ORDER_ANY, sve_init, NULL);
