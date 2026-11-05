/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host check for the R5 GPU interface layout and the VMID 1 walk.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "r5_gpu.h"
#include "r5_vm.h"

static int
fail(const char* what)
{
	fprintf(stderr, "fail %s\n", what);
	return 1;
}

int
main()
{
	static_assert(R5_GPU_OP_BASE == 9999u + 0x5000u, "ioctl base");
	static_assert(R5_GPU_GET_INFO == 30479u, "GET_INFO number");
	static_assert(R5_GPU_BO_IMPORT == 30479u + 18u, "last v1 call");
	static_assert(offsetof(r5_gpu_info, size) == 0, "info size");
	static_assert(offsetof(r5_gpu_info, version) == 4, "info version");
	static_assert(offsetof(r5_gpu_info, va_offset) == 48, "va offset");
	static_assert(offsetof(r5_gpu_cs, sequence) == 32, "cs sequence");
	static_assert(R5_GPU_VA_OFFSET == 0x10000ull, "va bottom");
	static_assert(R5_GPU_VA_MAX == 0x800000000000ull, "va max");
	static_assert(R5_GPU_HIGH_VA_OFFSET == 0xffff800000000000ull, "high");
	static_assert(R5_GPU_HIGH_VA_MAX == 0xffffffffffbfe000ull, "high max");

	const uint64_t pages = 262144ull << 18;
	const uint64_t bytes = pages * 4096ull - 0x402000ull;
	const uint64_t hole = 0x800000000000ull;
	const uint64_t holeEnd = 0xffff800000000000ull;
	if ((bytes < hole ? bytes : hole) != R5_GPU_VA_MAX)
		return fail("va max formula");
	if ((holeEnd | bytes) != R5_GPU_HIGH_VA_MAX)
		return fail("high max formula");

	const uint64_t va = 0xffff800000001000ull;
	if (r5_vm_index(va, 0) != 1)
		return fail("pt");
	if (r5_vm_index(va, 1) != 0 || r5_vm_index(va, 2) != 0)
		return fail("pdb");
	if (r5_vm_index(va, 3) != 0x100)
		return fail("pdb2");

	const uint64_t pde = r5_vm_pde(0x812c18000ull);
	if (pde != 0x812c18001ull)
		return fail("pde");
	if (!r5_gfx9_pde_aligned(0x812c18000ull))
		return fail("align");
	const uint64_t pte = r5_gfx9_gart_pte(0x123450000ull,
		R5_GFX9_PTE_VALID | R5_GFX9_PTE_SYSTEM | R5_GFX9_PTE_SNOOPED
		| R5_GFX9_PTE_EXECUTABLE | R5_GFX9_PTE_READABLE
		| R5_GFX9_PTE_WRITEABLE);
	if (pte != 0x0600000123450077ull)
		return fail("pte");

	printf("ok gpu api %u.%u walk\n", R5_GPU_API_MAJOR, R5_GPU_API_MINOR);
	return 0;
}
