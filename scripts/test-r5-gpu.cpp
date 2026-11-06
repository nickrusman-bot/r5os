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
#include "r5_cs.h"
#include "r5_span.h"
#include "r5_vm.h"
#include "r5_wait.h"

#include <string.h>

static int
fail(const char* what)
{
	fprintf(stderr, "fail %s\n", what);
	return 1;
}


static int
source_order(const char* path, const char** marks, int count)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return fail(path);
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return fail(path);
	}
	long size = ftell(file);
	if (size < 0 || size > 2000000) {
		fclose(file);
		return fail(path);
	}
	rewind(file);
	char* text = (char*)malloc((size_t)size + 1);
	if (text == NULL) {
		fclose(file);
		return fail(path);
	}
	if (fread(text, 1, (size_t)size, file) != (size_t)size) {
		free(text);
		fclose(file);
		return fail(path);
	}
	text[size] = 0;
	fclose(file);
	const char* cursor = text;
	for (int i = 0; i < count; i++) {
		const char* found = strstr(cursor, marks[i]);
		if (found == NULL) {
			fprintf(stderr, "fail order %s in %s\n", marks[i], path);
			free(text);
			return 1;
		}
		cursor = found + strlen(marks[i]);
	}
	free(text);
	return 0;
}

int
main()
{
	static_assert(R5_GPU_OP_BASE == 9999u + 0x5000u, "ioctl base");
	static_assert(R5_GPU_GET_INFO == 30479u, "GET_INFO number");
	static_assert(R5_GPU_BO_IMPORT == 30479u + 18u, "last v1 call");
	static_assert(R5_GPU_SELFTEST == 30479u + 23u, "selftest");
	static_assert(offsetof(r5_gpu_info, size) == 0, "info size");
	static_assert(offsetof(r5_gpu_info, version) == 4, "info version");
	static_assert(offsetof(r5_gpu_info, va_offset) == 48, "va offset");
	static_assert(offsetof(r5_gpu_cs, ibs) == 32, "cs ibs");
	static_assert(offsetof(r5_gpu_cs, sequence) == 56, "cs sequence");
	static_assert(offsetof(r5_gpu_va, bo_offset) == 24, "va bo offset");
	static_assert(offsetof(r5_gpu_va, map_bytes) == 32, "va bytes");
	static_assert(offsetof(r5_amdgpu_info_device, external_rev) == 8, "rev");
	static_assert(offsetof(r5_amdgpu_info_device, family) == 16, "family");
	static_assert(offsetof(r5_amdgpu_info_device, high_va_offset) == 336,
		"high va");
	static_assert(sizeof(r5_amdgpu_info_device) == 440, "device block");
	static_assert(sizeof(r5_amdgpu_info_firmware) == 8, "fw");
	static_assert(sizeof(r5_amdgpu_info_hw_ip) == 32, "hw ip");
	static_assert(R5_AMDGPU_FAMILY_RV == 142u, "raven family");
	if (r5_amd_external_rev(0x15e7u, 2u) != 0xa3u)
		return fail("barcelo rev");
	if (r5_amd_external_rev(0x1636u, 1u) != 0x92u)
		return fail("renoir rev");
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
	const uint64_t cached = r5_gfx9_cached_gtt_pte(0x123450000ull,
		R5_GFX9_PTE_VALID | R5_GFX9_PTE_EXECUTABLE | R5_GFX9_PTE_READABLE
		| R5_GFX9_PTE_WRITEABLE);
	if (cached != 0x0000000123450077ull)
		return fail("cached gtt");
	const uint64_t highIb = 0xffffffffffbfb000ull;
	const uint64_t clipped = (highIb & 0xffffffffull)
		| (((highIb >> 32) & 0xffffull) << 32);
	if (r5_vm_index(highIb, 0) != r5_vm_index(clipped, 0)
		|| r5_vm_index(highIb, 1) != r5_vm_index(clipped, 1)
		|| r5_vm_index(highIb, 2) != r5_vm_index(clipped, 2)
		|| r5_vm_index(highIb, 3) != r5_vm_index(clipped, 3))
		return fail("high va walk");

	struct host_page {
		uint64_t	cpu[512];
		uint64_t	physical;
	};
	static host_page tablePages[8];
	static int nextPage = 0;
	struct alloc_state {
		host_page*	pages;
		int*		next;
	};
	alloc_state state = { tablePages, &nextPage };
	auto alloc = [](void* cookie, uint64_t* physical, uint64_t** cpu) -> int {
		alloc_state* st = (alloc_state*)cookie;
		if (*st->next >= 8)
			return 0;
		host_page* page = &st->pages[*st->next];
		page->physical = 0x900000000ull + (uint64_t)(*st->next) * 4096ull;
		*st->next += 1;
		*physical = page->physical;
		*cpu = page->cpu;
		return 1;
	};
	r5_vm_page slots[8];
	r5_vm vm;
	memset(&vm, 0, sizeof(vm));
	vm.pages = slots;
	vm.capacity = 8;
	vm.alloc = alloc;
	vm.cookie = &state;
	const uint64_t leaf = 0x0600000123450077ull;
	if (!r5_vm_map_page(&vm, va, leaf))
		return fail("map");
	uint64_t got = 0;
	if (!r5_vm_lookup(&vm, va, &got) || got != leaf)
		return fail("lookup");
	if (r5_vm_index(va, 3) != 0x100)
		return fail("mapped pdb2");

	const uint64_t holeMask = 0x0000ffffffffffffull;
	const uint64_t highFirst = R5_GPU_HIGH_VA_OFFSET;
	const uint64_t highLast = R5_GPU_HIGH_VA_MAX - 4096ull;
	if (r5_vm_index(highFirst, 0) != r5_vm_index(highFirst & holeMask, 0)
		|| r5_vm_index(highFirst, 3) != r5_vm_index(highFirst & holeMask, 3))
		return fail("high first mask");
	if (r5_vm_index(highLast, 0) != r5_vm_index(highLast & holeMask, 0)
		|| r5_vm_index(highLast, 3) != r5_vm_index(highLast & holeMask, 3))
		return fail("high last mask");
	static host_page highPages[16];
	static int highNext = 0;
	alloc_state highState = { highPages, &highNext };
	r5_vm_page highSlots[16];
	r5_vm highVm;
	memset(&highVm, 0, sizeof(highVm));
	highVm.pages = highSlots;
	highVm.capacity = 16;
	highVm.alloc = alloc;
	highVm.cookie = &highState;
	if (!r5_vm_map_page(&highVm, highFirst, leaf))
		return fail("high first map");
	if (!r5_vm_map_page(&highVm, highLast, leaf))
		return fail("high last map");
	uint64_t highGot = 0;
	if (!r5_vm_lookup(&highVm, highFirst, &highGot) || highGot != leaf)
		return fail("high first lookup");
	if (!r5_vm_lookup(&highVm, highLast, &highGot) || highGot != leaf)
		return fail("high last lookup");
	uint64_t root = slots[0].cpu[0x100];
	if ((root & 1ull) == 0 || (root & ~0xfffull) == 0)
		return fail("mapped pde");

	uint32_t packet[12];
	const uint64_t ib = 0xffff800000001000ull;
	const uint64_t fenceMc = 0x812c18000ull + kR5UserFenceOffset;
	r5_cs_gfx_ib(packet, ib, 16, 1, fenceMc, 7);
	if (packet[2] != 0x8000u)
		return fail("ib high");
	if ((packet[3] & (0xfu << 24)) != (1u << 24))
		return fail("vmid");
	if ((packet[3] & (1u << 23)) != 0)
		return fail("valid bit");
	if ((packet[3] & 0xfffffu) != 16u)
		return fail("ib length");
	if (packet[6] != ((2u << 29) | (2u << 24)))
		return fail("fence sel");
	if (packet[7] != (uint32_t)fenceMc || packet[8] != (uint32_t)(fenceMc >> 32)
		|| packet[9] != 7u)
		return fail("sequence");
	if ((fenceMc & 7ull) != 0)
		return fail("fence align");
	if (r5_range_overlap(0, 4096, 4096, 4096))
		return fail("no overlap");
	if (!r5_range_overlap(0, 8192, 4096, 4096))
		return fail("overlap");
	r5_span spans[4];
	memset(spans, 0, sizeof(spans));
	if (!r5_span_add(spans, 4, 0x1000, 0x3000))
		return fail("span add");
	r5_span_free(spans, 4, 0x1000);
	uint64_t taken = 0;
	if (!r5_span_take(spans, 4, 0x1000, &taken) || taken != 0x1000)
		return fail("span take");
	r5_span_free(spans, 4, taken);
	if (r5_wait_step(5, 5, 10, 0) != R5_WAIT_DONE)
		return fail("wait done");
	if (r5_wait_step(1, 5, 0, 0) != R5_WAIT_POLL)
		return fail("wait poll");
	if (r5_wait_step(1, 5, 10, 1) != R5_WAIT_TIMEOUT)
		return fail("wait timeout");

	const char* selftestOrder[] = {
		"highVa, false",
		"\"vulkaninfo\"",
		"VulkanTeapot (sam)",
		"\"VulkanTeapot\"",
		"highVa, true",
		"stage_selftest(out)"
	};
	const char* stageOrder[] = {
		"\"u1\"",
		"gpu_stage_u2a",
		"\"u4\"",
		"\"u3\"",
		"\"u2\"",
		"gpu_stage_fault"
	};
	if (source_order(
			"distro/graphics/src/bin/r5-gpu-selftest/r5-gpu-selftest.cpp",
			selftestOrder, 6) != 0)
		return 1;
	if (source_order(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
			stageOrder, 6) != 0)
		return 1;

	printf("ok gpu api %u.%u walk\n", R5_GPU_API_MAJOR, R5_GPU_API_MINOR);
	return 0;
}
