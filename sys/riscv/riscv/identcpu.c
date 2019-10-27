/*-
 * Copyright (c) 2015-2016 Ruslan Bukin <br@bsdpad.com>
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
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/pcpu.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/elf.h>
#include <machine/md_var.h>
#include <machine/trap.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

char machine[] = "riscv";

SYSCTL_STRING(_hw, HW_MACHINE, machine, CTLFLAG_RD, machine, 0,
    "Machine class");

/* Hardware implementation info. */
register_t mvendorid = MVENDORID_UNKNOWN;	/* The CPU's JEDEC vendor ID */
register_t marchid = MARCHID_UNKNOWN;		/* The CPU's architecture ID */
register_t mimpid = MIMPID_UNKNOWN;		/* The CPU's implementation ID */

static void get_isa_info(u_int cpu);
static void get_vendor_info(u_int cpu);
static void get_arch_info(u_int cpu);
static void get_impl_info(u_int cpu);

struct cpu_desc {
	u_int		cpu_impl;
	const char	*cpu_vendor_name;
	const char	*cpu_isa;
	const char	*cpu_arch_name;
};

struct cpu_desc cpu_desc[MAXCPU];

/*
 * Vendors table.
 */
static const struct {
	u_int		id;
	const char	*name;
} riscv_vendor_table[] = {
	{ MVENDORID_SIFIVE,	"SiFive" },
	{ MVENDORID_UNKNOWN,	"Unknown Vendor" }
};

/*
 * Micro-architectures table.
 */
static const struct {
	u_int		id;
	const char	*name;
} riscv_arch_table[] = {
	{ MARCHID_ROCKET,	"UC Berkeley Rocket" },
	{ MARCHID_UNKNOWN,	"Unknown micro-architecture" },
};

#define	ISA_PREFIX		("rv" __XSTRING(__riscv_xlen))
static void
get_isa_info(u_int cpu)
{
	/* Just write the prefix for now. */
	cpu_desc[cpu].cpu_isa = ISA_PREFIX;
}

static void
get_vendor_info(u_int cpu)
{
	u_int i;

	/* Search for the vendor string. */
	for (i = 0; riscv_vendor_table[i].id != MVENDORID_UNKNOWN; i++) {
		if (mvendorid == riscv_vendor_table[i].id) {
			cpu_desc[cpu].cpu_vendor_name =
			    riscv_vendor_table[i].name;
			return;
		}
	}

	cpu_desc[cpu].cpu_vendor_name = "Unknown Vendor";
}

static void
get_arch_info(u_int cpu)
{
	u_int i;

	for (i = 0; riscv_arch_table[i].id != MARCHID_UNKNOWN; i++) {
		if (marchid == riscv_arch_table[i].id) {
			cpu_desc[cpu].cpu_arch_name = riscv_arch_table[i].name;
			return;
		}
	}

	/* If we didn't find a match, just print the value. */
	cpu_desc[cpu].cpu_arch_name = "Unknown Micro-Architecture";
}

static void
get_impl_info(u_int cpu)
{

	cpu_desc[cpu].cpu_impl = mimpid;
}

#ifdef FDT
/*
 * The ISA string is made up of a small prefix (e.g. rv64) and up to 26 letters
 * indicating the presence of the 26 possible standard extensions. Therefore 32
 * characters will be sufficient.
 */
#define	ISA_NAME_MAXLEN		32
#define	ISA_PREFIX_LEN		(sizeof(ISA_PREFIX) - 1)

static void
fill_elf_hwcap(void *dummy __unused)
{
	u_long caps[256] = {0};
	char isa[ISA_NAME_MAXLEN];
	u_long hwcap;
	phandle_t node;
	ssize_t len;
	int i;

	caps['i'] = caps['I'] = HWCAP_ISA_I;
	caps['m'] = caps['M'] = HWCAP_ISA_M;
	caps['a'] = caps['A'] = HWCAP_ISA_A;
#ifdef FPE
	caps['f'] = caps['F'] = HWCAP_ISA_F;
	caps['d'] = caps['D'] = HWCAP_ISA_D;
#endif
	caps['c'] = caps['C'] = HWCAP_ISA_C;

	node = OF_finddevice("/cpus");
	if (node == -1) {
		if (bootverbose)
			printf("fill_elf_hwcap: Can't find cpus node\n");
		return;
	}

	/*
	 * Iterate through the CPUs and examine their ISA string. While we
	 * could assign elf_hwcap to be whatever the boot CPU supports, to
	 * handle the (unusual) case of running a system with heterogeneous
	 * ISAs, keep only the extension bits that are common to all harts.
	 */
	for (node = OF_child(node); node > 0; node = OF_peer(node)) {
		/* Skip any non-CPU nodes, such as cpu-map. */
		if (!ofw_bus_node_is_compatible(node, "riscv"))
			continue;

		len = OF_getprop(node, "riscv,isa", isa, sizeof(isa));
		KASSERT(len <= ISA_NAME_MAXLEN, ("ISA string truncated"));
		if (len == -1) {
			if (bootverbose)
				printf("fill_elf_hwcap: "
				    "Can't find riscv,isa property\n");
			return;
		} else if (strncmp(isa, ISA_PREFIX, ISA_PREFIX_LEN) != 0) {
			if (bootverbose)
				printf("fill_elf_hwcap: "
				    "Unsupported ISA string: %s\n", isa);
			return;
		}

		hwcap = 0;
		for (i = ISA_PREFIX_LEN; i < len; i++)
			hwcap |= caps[(unsigned char)isa[i]];

		if (elf_hwcap != 0)
			elf_hwcap &= hwcap;
		else
			elf_hwcap = hwcap;
	}
}

SYSINIT(identcpu, SI_SUB_CPU, SI_ORDER_ANY, fill_elf_hwcap, NULL);
#endif

void
identify_cpu(void)
{
	struct cpu_desc desc;
	u_int cpu;

	cpu = PCPU_GET(cpuid);
	
	/* Fill the cpu descriptor. */
	get_isa_info(cpu);
	get_vendor_info(cpu);
	get_arch_info(cpu);
	get_impl_info(cpu);

	/* Print details for boot CPU or if we want verbose output. */
	if (cpu == 0 || bootverbose) {
		desc = cpu_desc[cpu];
		printf("CPU(%d): %s\n"
		    "Vendor: %s\n"
		    "Micro-architecture: %s\n"
		    "Implementation: %lu\n",
		    cpu, desc.cpu_isa, desc.cpu_vendor_name,
		    desc.cpu_arch_name, desc.cpu_impl);
		printf("\n");
	}
}
