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
#include "r5_pasid.h"
#include "r5_vmid.h"
#include "r5_defer.h"
#include "r5_gc9_regs.h"
#include "r5_agp.h"
#include "r5_inv_range.h"
#include "r5_vm_size.h"
#include "r5_span.h"
#include "r5_vm.h"
#include "r5_wait.h"
#include "r5_stage_p.h"
#include "r5_stage_r1.h"
#include "r5_log.h"

#include <string.h>

static int fail(const char* what);

static int
body_skip(int index)
{
	/* IB low/high, IB length, both fence addresses, both sequences. */
	if (index == 6 || index == 7 || index == 10 || index == 11)
		return 1;
	if (index == 8 || index == 12)
		return 2;
	if (index == 18 || index == 19 || index == 26 || index == 27)
		return 1;
	if (index == 20 || index == 21 || index == 28)
		return 3;
	return 0;
}


static int
pad_form(const uint32_t* words, uint32_t count, const char* who)
{
	uint32_t i;
	uint32_t n;
	if (count == 0)
		return fail(who);
	if (count == 1) {
		if (words[0] != kR5CsNop)
			return fail(who);
		return 0;
	}
	n = count - 2u;
	if (n > 0x3ffeu)
		n = 0x3ffeu;
	if (words[0] != r5_cs_packet3(0x10, n)) {
		fprintf(stderr, "fail %s header %08x want %08x count %u\n",
			who, words[0], r5_cs_packet3(0x10, n), count);
		return 1;
	}
	for (i = 1; i < count; i++) {
		if (words[i] != kR5CsNop)
			return fail(who);
	}
	return 0;
}


/* The job r5_gfx9_user_submit writes: r5_cs_user_body plus r5_cs_pad.
 * Compared with ring-after-2 from CONTEXT_CONTROL through the last NOP. */
static int
linux_ring_frame()
{
	const char* path =
		"generated/linux-ref/capture-5625u/capture-20261106-194121/"
		"ring-after-2-amdgpu_ring_gfx.bin";
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return fail(path);
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return fail(path);
	}
	long bytes = ftell(file);
	if (bytes < 16 || (bytes % 4) != 0) {
		fclose(file);
		return fail("ring size");
	}
	rewind(file);
	uint32_t* ring = (uint32_t*)malloc((size_t)bytes);
	if (ring == NULL) {
		fclose(file);
		return fail("ring alloc");
	}
	if (fread(ring, 1, (size_t)bytes, file) != (size_t)bytes) {
		free(ring);
		fclose(file);
		return fail("ring read");
	}
	fclose(file);
	uint32_t dwords = (uint32_t)(bytes / 4);
	if (ring[0] != ring[1] || ring[1] != ring[2]) {
		free(ring);
		return fail("ring header");
	}
	int start = -1;
	for (uint32_t i = 3; i + 4 < dwords; i++) {
		if (ring[i] == 0xc0012800u && ring[i + 1] == 0x80000000u
			&& ring[i + 2] == 0 && ring[i + 3] == 0xc0009000u)
			start = (int)i;
	}
	if (start < 3) {
		free(ring);
		return fail("context");
	}
	uint32_t body = 33;
	if ((uint32_t)start + body >= dwords
		|| ring[start + 31] != 0xc0008b00u || ring[start + 32] != 0) {
		free(ring);
		return fail("switch");
	}
	uint32_t padAt = (uint32_t)start + body;
	uint32_t padEnd = padAt + 1;
	while (padEnd < dwords && ring[padEnd] == kR5CsNop)
		padEnd++;
	uint32_t linuxPad = padEnd - padAt;
	if (pad_form(ring + padAt, linuxPad, "linux pad") != 0) {
		free(ring);
		return 1;
	}
	uint64_t wptr = (uint64_t)start - 3ull;
	const uint64_t ibs[] = {
		0xffff800100099000ull, 0xffff800100085000ull
	};
	const uint32_t ibDw[] = { 0x18u, 0x408u };
	const uint32_t ibVm[] = { 1u, 1u };
	if (ring[0x2f] != 0xc0008b00u || ring[0x30] != 0
		|| ring[0x31] != 0xc0008b00u || ring[0x32] != 0
		|| ring[0x33] != 0xc0032200u) {
		free(ring);
		return fail("vm flush switches");
	}
	if (ring[0x46] != 0xc0012800u || ring[0x47] != kR5CsCntxLoad) {
		free(ring);
		return fail("load bits");
	}
	uint32_t job[512];
	uint32_t filled = r5_cs_user_body(job, 512, ibs, ibDw, ibVm, 2,
		0x66d000ull, 3, 0x401040ull, kR5CsCntxKeep, 0);
	if (filled != body + 4u || job[0] != 0xc0008b00u || job[1] != 0
		|| job[2] != 0xc0008b00u || job[3] != 0
		|| job[5] != kR5CsCntxKeep) {
		free(ring);
		return fail("two ib body");
	}
	uint32_t* slice = job + 4;
	uint32_t oursPad = r5_cs_pad_dwords(wptr + body);
	uint32_t padWords[256];
	if (oursPad > 256) {
		free(ring);
		return fail("job cap");
	}
	r5_cs_pad(padWords, oursPad);
	if (pad_form(padWords, oursPad, "ours pad") != 0) {
		free(ring);
		return 1;
	}
	int diffs = 0;
	for (uint32_t i = 0; i < body; i++) {
		int skip = body_skip((int)i);
		if (skip == 2) {
			if (((slice[i] ^ ring[start + i]) & ~0xfffffu) != 0) {
				fprintf(stderr, "fail ib control +%u linux %08x ours %08x\n",
					i, ring[start + i], slice[i]);
				free(ring);
				return 1;
			}
			continue;
		}
		if (skip != 0) {
			if (slice[i] != ring[start + i]) {
				printf("diff +%03x linux %08x ours %08x %s\n",
					i, ring[start + i], slice[i],
					skip == 3 ? "sequence" : "address");
				diffs++;
			}
			continue;
		}
		if (slice[i] != ring[start + i]) {
			fprintf(stderr, "fail frame +%u linux %08x ours %08x\n",
				i, ring[start + i], slice[i]);
			free(ring);
			return 1;
		}
	}
	if (oursPad == linuxPad) {
		for (uint32_t i = 0; i < oursPad; i++) {
			if (padWords[i] != ring[padAt + i]) {
				fprintf(stderr, "fail nop +%u linux %08x ours %08x\n",
					i, ring[padAt + i], padWords[i]);
				free(ring);
				return 1;
			}
		}
	}
	printf("linux frame file %x..%x wptr %llx body %u pad linux %u ours %u header %08x diffs %d\n",
		start, padEnd - 1, (unsigned long long)wptr, body, linuxPad, oursPad,
		padWords[0], diffs);

	const uint64_t otherIb[] = { 0x1000ull, 0x2000ull };
	const uint32_t otherDw[] = { 4u, 8u };
	uint32_t other[320];
	uint32_t otherN = r5_cs_user_body(other, 320, otherIb, otherDw, ibVm, 2,
		0xabc000ull, 9, 0xabd000ull, kR5CsCntxKeep, 0);
	uint32_t load[64];
	uint32_t loadN = r5_cs_user_body(load, 64, otherIb, otherDw, ibVm, 1,
		0xabc000ull, 9, 0xabd000ull, kR5CsCntxLoad, 0);
	if (loadN != r5_cs_user_words(1) || load[5] != kR5CsCntxLoad
		|| load[6] != 0)
		return fail("switch load");
	uint32_t otherPad = r5_cs_pad_dwords(otherN - 4u);
	uint32_t otherPadWords[256];
	r5_cs_pad(otherPadWords, otherPad);
	if (otherPad == linuxPad)
		return fail("pad count changed");
	for (uint32_t i = 0; i < body; i++) {
		int skip = body_skip((int)i);
		if (skip == 0 && other[i + 4] != ring[start + i]) {
			free(ring);
			return fail("shifted frame");
		}
		if (skip == 2 && ((other[i + 4] ^ ring[start + i]) & ~0xfffffu) != 0) {
			free(ring);
			return fail("shifted vmid");
		}
	}
	if (pad_form(otherPadWords, otherPad, "shifted pad") != 0) {
		free(ring);
		return 1;
	}
	printf("shifted wptr 0 pad %u header %08x (count may differ)\n",
		otherPad, otherPadWords[0]);
	free(ring);
	return 0;
}


static int
snippet(const char* path, const char* begin, const char* end, char* out,
	size_t cap)
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
	const char* from = strstr(text, begin);
	const char* to = from == NULL ? NULL : strstr(from, end);
	if (from == NULL || to == NULL || (size_t)(to - from) >= cap) {
		free(text);
		return fail(begin);
	}
	memcpy(out, from, (size_t)(to - from));
	out[to - from] = 0;
	free(text);
	return 0;
}


/* The wait reads the 64-bit user fence. The EOP interrupt only wakes
 * retire, and retire reads that same word. The 32-bit INT_SEL 2 fence
 * is not the value the waiter compares. */
static int
waiter_fence()
{
	char text[4000];
	if (snippet(
			"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp",
			"r5_gfx9_user_seen()", "r5_gfx9_user_fence_mc()", text,
			sizeof(text)) != 0)
		return 1;
	if (strstr(text, "kR5UserFenceOffset") == NULL
		|| strstr(text, "volatile uint64") == NULL
		|| strstr(text, "kR5UserIrqOffset") != NULL)
		return fail("user seen");
	if (snippet(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/gfx9_k12.cpp",
			"entry.source == kSourceEop", "sEopCount++", text, sizeof(text)) != 0)
		return 1;
	if (strstr(text, "r5_gpu_retire_wake()") == NULL)
		return fail("eop wake");
	if (snippet(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
			"r5_gpu_eop()", "gpu_counter(", text, sizeof(text)) != 0)
		return 1;
	if (strstr(text, "r5_gfx9_user_seen()") == NULL)
		return fail("retire fence");
	if (snippet(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
			"gpu_counter(", "gpu_wait_value(", text, sizeof(text)) != 0)
		return 1;
	if (strstr(text, "return r5_gfx9_user_seen()") == NULL)
		return fail("wait fence");
	if ((kR5CsRel64 & 0x03000000u) != 0
		|| (kR5CsRel32Int & 0x03000000u) != 0x02000000u)
		return fail("int sel");
	if (kR5UserIrqOffset != kR5UserFenceOffset + 16u)
		return fail("fence slots");
	return 0;
}


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
	{
		const char* reported[] = {
			"out.high_va_offset = R5_GPU_HIGH_VA_OFFSET",
			"out.high_va_max = R5_GPU_HIGH_VA_MAX",
			"amd.device.high_va_offset = R5_GPU_HIGH_VA_OFFSET",
			"amd.device.high_va_max = R5_GPU_HIGH_VA_MAX"
		};
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
				reported, 4) != 0)
			return 1;
	}

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
	const uint64_t access = R5_GFX9_PTE_VALID | R5_GFX9_PTE_EXECUTABLE
		| R5_GFX9_PTE_READABLE | R5_GFX9_PTE_WRITEABLE;
	const uint64_t wc = r5_gfx9_user_pte(0x123450000ull, access, 1, 1,
		&kR5Gfx93Mtype);
	if (wc != 0x0000000123450073ull)
		return fail("wc gtt");
	if ((wc & (uint64_t)R5_GFX9_PTE_SNOOPED) != 0)
		return fail("wc snoop");
	if ((cached & (uint64_t)R5_GFX9_PTE_SNOOPED) == 0)
		return fail("cached snoop");
	const uint64_t vram = r5_gfx9_user_pte(0x123450000ull,
		access | (uint64_t)R5_GFX9_PTE_SYSTEM | (uint64_t)R5_GFX9_PTE_SNOOPED,
		0, 0, &kR5Gfx93Mtype);
	if (vram != pte)
		return fail("vram stays uc");
	if (kR5Gfx93Mtype.gc_major != 9 || kR5Gfx93Mtype.gc_minor != 3
		|| kR5Gfx93Mtype.system_mtype != 0 || kR5Gfx93Mtype.vram_mtype != 0
		|| kR5Gfx93Mtype.vram_live != 0 || kR5Gfx93Mtype.source == NULL)
		return fail("mtype policy");
	if (r5_gfx9_cpu_memory_type(1) != (2u << 28)
		|| r5_gfx9_cpu_memory_type(0) != 0)
		return fail("cpu memory type");
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
	if (tablePages[0].cpu[1] != kR5VmPdeEmpty)
		return fail("empty pd");
	if (tablePages[3].cpu[r5_vm_index(va, 0) == 0 ? 1 : 0] != kR5VmPteEmpty)
		return fail("empty pte");
	if (!r5_vm_map_page(&vm, va, 0))
		return fail("unmap");
	got = 0;
	if (r5_vm_lookup(&vm, va, &got) != 0 || got != kR5VmPteEmpty)
		return fail("unmapped pte");
	if (!r5_vm_map_page(&vm, va, leaf))
		return fail("remap");
	if (!r5_vm_lookup(&vm, va, &got) || got != leaf)
		return fail("remap lookup");
	if (!r5_vm_map_page(&vm, va + 4096, kR5Gfx9PtePrt))
		return fail("prt map");
	if (!r5_vm_map_page(&vm, va + 4096, 0))
		return fail("prt clear");
	got = 0;
	if (r5_vm_lookup(&vm, va + 4096, &got) != 0 || got != kR5Gfx9PtePrt)
		return fail("prt stays");
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

	uint32_t packet[64];
	const uint64_t ib = 0xffff800000001000ull;
	const uint64_t ibs[] = { ib };
	const uint32_t ibDw[] = { 16u };
	const uint32_t ibVm[] = { 1u };
	const uint64_t fenceMc = 0x812c18000ull + kR5UserFenceOffset;
	const uint64_t irqMc = 0x812c18000ull + kR5UserIrqOffset;
	uint32_t body = r5_cs_user_body(packet, 64, ibs, ibDw, ibVm, 1, fenceMc, 7,
		irqMc, kR5CsCntxKeep, 0);
	if (body != 33u || body != r5_cs_user_words(1))
		return fail("frame size");
	if (packet[0] != r5_cs_packet3(0x8b, 0) || packet[1] != 0
		|| packet[2] != r5_cs_packet3(0x8b, 0) || packet[3] != 0)
		return fail("open switch");
	if (packet[4] != r5_cs_packet3(0x28, 1) || packet[5] != kR5CsCntxKeep
		|| packet[6] != 0)
		return fail("context");
	if (packet[7] != r5_cs_packet3(0x90, 0) || packet[8] != 0)
		return fail("frame start");
	if (packet[9] != r5_cs_packet3(0x3f, 2) || packet[10] != (uint32_t)ib
		|| packet[11] != 0xffff8000u)
		return fail("ib high");
	if ((packet[12] & (0xfu << 24)) != (1u << 24))
		return fail("vmid");
	if ((packet[12] & (1u << 23)) != 0)
		return fail("valid bit");
	if ((packet[12] & 0xfffffu) != 16u)
		return fail("ib length");
	if (packet[13] != r5_cs_packet3(0x90, 0) || packet[14] != 0x10000000u)
		return fail("frame end");
	if (packet[15] != r5_cs_packet3(0x49, 6) || packet[16] != kR5CsEvent
		|| packet[17] != kR5CsRel64)
		return fail("user fence");
	if (packet[18] != (uint32_t)fenceMc || packet[19] != (uint32_t)(fenceMc >> 32)
		|| packet[20] != 7u)
		return fail("sequence");
	if (packet[23] != r5_cs_packet3(0x49, 6) || packet[25] != kR5CsRel32Int
		|| packet[26] != (uint32_t)irqMc || packet[28] != 7u)
		return fail("irq fence");
	if (packet[31] != r5_cs_packet3(0x8b, 0) || packet[32] != 0)
		return fail("switch");
	if (r5_cs_user_commit(0, 1) != 256u)
		return fail("pad align");
	uint32_t pad[8];
	r5_cs_pad(pad, 1);
	if (pad[0] != kR5CsNop)
		return fail("pad one");
	r5_cs_pad(pad, 3);
	if (pad[0] != r5_cs_packet3(0x10, 1) || pad[1] != kR5CsNop
		|| pad[2] != kR5CsNop)
		return fail("pad form");
	/* Stick 15 wrote PACKET3(NOP, 44) for a 45-dword pad (c02c1000). */
	uint32_t pad45[45];
	r5_cs_pad(pad45, 45);
	if (pad45[0] != r5_cs_packet3(0x10, 43) || pad45[44] != kR5CsNop)
		return fail("pad 45");
	if (linux_ring_frame() != 0)
		return 1;
	if (waiter_fence() != 0)
		return 1;
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
	if (r5_pace(0, 1) != R5_PACE_OK || r5_pace(999999ull, 1) != R5_PACE_OK)
		return fail("pace ok");
	if (r5_pace(1000000ull, 1) != R5_PACE_LATE
		|| r5_pace(4999999ull, 1) != R5_PACE_LATE)
		return fail("pace late");
	if (r5_pace(1000000ull, 0) != R5_PACE_WAIT)
		return fail("pace wait");
	if (r5_pace(5000000ull, 0) != R5_PACE_TIMEOUT)
		return fail("pace timeout");

	const char* selftestOrder[] = {
		"highVa, false",
		"stage_regs(out)",
		"stage_r1(out, R5_GPU_R1_AB, 0xffff800000100000ull)",
		"R5_GPU_SWITCH_INV_RANGE",
		"stage_r1(out, R5_GPU_R1_AB, 0xffff800000300000ull)",
		"\"vulkaninfo\"",
		"\"VulkanTeapot\"",
		"VulkanTeapot (sam)",
		"\"VulkanTeapot\"",
		"R5_GPU_SWITCH_FLUSH_FENCE",
		"confirm teapots",
		"stage_r1(out, R5_GPU_R1_C, 0xffff800000500000ull)",
		"stage_selftest(out)",
		"highVa, true"
	};
	const char* stageOrder[] = {
		"gpu_stage_p(",
		"gpu_stage_u(",
		"gpu_stage_hdp(info)",
		"\"b1\"",
		"\"b2\"",
		"\"b3\"",
		"\"b5\"",
		"\"a2\"",
		"\"a1\"",
		"\"a3\"",
		"\"a8\"",
		"\"a7\"",
		"\"a4\"",
		"\"a5\"",
		"\"a6\"",
		"\"c1\""
	};
	/* Linux 6.12 gfxhub_v1_0_init_tlb_regs, from the same prior 0x2501.
	 * Mask 0x3879 is the fields it writes. ECO stays. */
	static_assert(((0x2501u & ~0x3879u) | 0x3859u) == 0x3d59u, "l1 tlb");
	static_assert((0x1018u ^ 0x18u) == 0x1000u, "sh mem retry");
	static_assert((0x007ffe87u ^ 0x007ffe07u) == 0x80u, "context retry");
	if (source_order(
			"distro/graphics/src/bin/r5-gpu-selftest/r5-gpu-selftest.cpp",
			selftestOrder, 14) != 0)
		return 1;
	{
		const char* regsBefore[] = {
			"stage_regs(out)", "stage_regs(out)", "stage_regs(out)",
			"stage_regs(out)", "stage_regs(out)", "stage_regs(out)",
			"stage_regs(out)", "stage_regs(out)", "stage_regs(out)",
			"stage_regs(out)", "stage_regs(out)"
		};
		if (source_order(
				"distro/graphics/src/bin/r5-gpu-selftest/r5-gpu-selftest.cpp",
				regsBefore, 11) != 0)
			return 1;
		const char* r1cLog[] = {
			"stage_regs(out); stage_r1(out, R5_GPU_R1_C, 0xffff800000500000ull)"
		};
		if (source_order(
				"distro/graphics/src/bin/r5-gpu-selftest/r5-gpu-selftest.cpp",
				r1cLog, 1) != 0)
			return 1;
		char logBody[2000];
		if (snippet(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/gfx9_k12.cpp",
				"r5_gfx9_log_state(", "r5_gfx9_inv_range_restore(",
				logBody, sizeof(logBody)) != 0)
			return 1;
		if (strstr(logBody, "open(") != NULL
			|| strstr(logBody, "fsync(") != NULL)
			return fail("kernel log file");
		if (strstr(logBody, "dprintf") == NULL)
			return fail("kernel log dprintf");
		if (source_order(
				"distro/graphics/src/bin/r5-gpu-selftest/r5-gpu-selftest.cpp",
				(const char*[]){ "fputs(in.line, out)", "fsync(fileno(out))" },
				2) != 0)
			return 1;
	}
	if (source_order(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
			stageOrder, 16) != 0)
		return 1;
	{
		char uBody[8000];
		if (snippet(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
				"gpu_stage_u(", "return B_OK;", uBody, sizeof(uBody)) != 0)
			return 1;
		if (strstr(uBody, "gpu_stage_r1(") != NULL)
			return fail("r1 still in u");
	}
	{
		FILE* driver = fopen(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
			"rb");
		if (driver == NULL)
			return fail("r5_gpu.cpp");
		if (fseek(driver, 0, SEEK_END) != 0) {
			fclose(driver);
			return fail("r5_gpu.cpp");
		}
		long bytes = ftell(driver);
		if (bytes < 0) {
			fclose(driver);
			return fail("r5_gpu.cpp");
		}
		rewind(driver);
		char* text = (char*)malloc((size_t)bytes + 1);
		if (text == NULL || fread(text, 1, (size_t)bytes, driver) != (size_t)bytes) {
			free(text);
			fclose(driver);
			return fail("r5_gpu.cpp");
		}
		text[bytes] = 0;
		fclose(driver);
		if (strstr(text, "B_WRITE_COMBINING_MEMORY") != NULL
			|| strstr(text, "\"u2c\"") != NULL
			|| strstr(text, "u2a-sh-lx") != NULL) {
			free(text);
			return fail("uswc still on");
		}
		free(text);
	}

	{
		/* REVIEW-25 X1. Four high addresses, GTT and VRAM. */
		static uint64_t pool_gtt[64][512];
		static uint64_t pool_vram[64][512];
		struct walk_cookie {
			int next;
			uint64_t (*pool)[512];
			uint64_t base;
		};
		auto alloc = [](void* raw, uint64_t* physical, uint64_t** cpu) -> int {
			walk_cookie* c = (walk_cookie*)raw;
			if (c->next >= 64)
				return 0;
			*physical = c->base + (uint64_t)c->next * 4096ull;
			*cpu = c->pool[c->next];
			c->next++;
			return 1;
		};
		const uint64_t vas[4] = {
			0xffff800000000000ull,
			0xffff800000200000ull,
			0xffff800100000000ull,
			R5_GPU_HIGH_VA_MAX - 0x1000ull
		};
		const uint64_t access = R5_GFX9_PTE_VALID | R5_GFX9_PTE_EXECUTABLE
			| R5_GFX9_PTE_READABLE | R5_GFX9_PTE_WRITEABLE;
		const uint64_t keep = access;
		for (int i = 0; i < 4; i++) {
			uint64_t masked = vas[i] & 0x0000ffffffffffffull;
			for (int level = 0; level < 4; level++) {
				if (r5_vm_index(vas[i], (uint32_t)level)
					!= r5_vm_index(masked, (uint32_t)level))
					return fail("x1 masked index");
			}
			if (!r5_vm_va_fits(vas[i], 4096))
				return fail("x1 fits");
			uint64_t page = 0;
			if (!r5_vm_page_va(vas[i], 0, &page) || page != vas[i])
				return fail("x1 page");
			if (!r5_vm_page_va(vas[i], 4096, &page)
				|| page != vas[i] + 4096ull)
				return fail("x1 filled");
			r5_vm_page pages_gtt[64];
			r5_vm_page pages_vram[64];
			memset(pool_gtt, 0, sizeof(pool_gtt));
			memset(pool_vram, 0, sizeof(pool_vram));
			walk_cookie gtt_cookie;
			gtt_cookie.next = 0;
			gtt_cookie.pool = pool_gtt;
			gtt_cookie.base = 0x900000000ull;
			r5_vm gtt;
			memset(&gtt, 0, sizeof(gtt));
			gtt.pages = pages_gtt;
			gtt.capacity = 64;
			gtt.alloc = alloc;
			gtt.cookie = &gtt_cookie;
			uint64_t gtt_pte = r5_gfx9_user_pte(0x123450000ull, access,
				1, 1, &kR5Gfx93Mtype);
			if (!r5_vm_map_page(&gtt, vas[i], gtt_pte))
				return fail("x1 gtt map");
			walk_cookie vram_cookie;
			vram_cookie.next = 0;
			vram_cookie.pool = pool_vram;
			vram_cookie.base = 0x900000000ull;
			r5_vm vram;
			memset(&vram, 0, sizeof(vram));
			vram.pages = pages_vram;
			vram.capacity = 64;
			vram.alloc = alloc;
			vram.cookie = &vram_cookie;
			uint64_t vram_pte = r5_gfx9_user_pte(0x123450000ull, access,
				0, 0, &kR5Gfx93Mtype);
			if (!r5_vm_map_page(&vram, vas[i], vram_pte))
				return fail("x1 vram map");
			if (gtt.used != vram.used || gtt.used != 4)
				return fail("x1 depth");
			printf("x1 va %016llx", (unsigned long long)vas[i]);
			for (int level = 3; level >= 0; level--) {
				printf(" L%d=%u", level,
					r5_vm_index(vas[i], (uint32_t)level));
			}
			for (uint32_t p = 0; p + 1 < gtt.used; p++) {
				uint32_t slot = r5_vm_index(vas[i], 3u - p);
				if (gtt.pages[p].cpu[slot] != vram.pages[p].cpu[slot])
					return fail("x1 pde");
				printf(" pde%u=%016llx", p,
					(unsigned long long)gtt.pages[p].cpu[slot]);
			}
			uint64_t got_gtt = 0;
			uint64_t got_vram = 0;
			if (!r5_vm_lookup(&gtt, vas[i], &got_gtt)
				|| !r5_vm_lookup(&gtt, masked, &got_gtt)
				|| !r5_vm_lookup(&vram, vas[i], &got_vram))
				return fail("x1 lookup");
			if ((got_gtt & keep) != (got_vram & keep))
				return fail("x1 pte bits");
			if ((got_gtt & (uint64_t)R5_GFX9_PTE_SYSTEM) == 0
				|| (got_gtt & (uint64_t)R5_GFX9_PTE_SNOOPED) != 0)
				return fail("x1 gtt flags");
			if ((got_vram & (uint64_t)R5_GFX9_PTE_SYSTEM) != 0)
				return fail("x1 vram system");
			if ((got_vram & (3ull << 57)) != (3ull << 57))
				return fail("x1 vram mtype");
			if ((got_gtt & (3ull << 57)) != 0)
				return fail("x1 gtt mtype");
			printf(" gtt=%016llx vram=%016llx\n",
				(unsigned long long)got_gtt,
				(unsigned long long)got_vram);
		}
		if (r5_vm_va_fits(0x800000000000ull, 4096))
			return fail("x1 hole");
		uint64_t wrapped = 0;
		if (r5_vm_page_va(0xfffffffffffff000ull, 0x2000ull, &wrapped))
			return fail("x1 overflow");
	}

	if (R5_P_COUNT != R5_GPU_P_STEPS)
		return fail("stage p count");
	{
		uint32_t words[32];
		uint32_t n = r5_stage_p_build(R5_P_WRITE_DATA, 0x50000ull, words, 32, 0);
		if (n != 6 || words[0] != 0xc0043700u || words[1] != 0x40100500u
			|| words[2] != 0x50000u)
			return fail("p write");
		n = r5_stage_p_build(R5_P_RELEASE_MEM, 0x50000ull, words, 32, 0);
		if (n < 8 || words[1] != 0x00220514u || words[2] != 0x23000000u)
			return fail("p release");
		n = r5_stage_p_build(R5_P_RSRC3_VS, 0x50000ull, words, 32, 0);
		if (n != 3 || words[0] != 0xc0017600u || words[1] != 0x30000046u
			|| words[2] != 0x003ffffeu)
			return fail("p rsrc3");
		n = r5_stage_p_build(R5_P_INDEX_PRIM, 0x50000ull, words, 32, 0);
		if (n != 3 || words[1] != 0x10000242u)
			return fail("p prim");
		n = r5_stage_p_build(R5_P_INDEX_TYPE, 0x50000ull, words, 32, 0);
		if (n != 3 || words[1] != 0x20000243u)
			return fail("p index");
		n = r5_stage_p_build(R5_P_DMA_DATA, 0x50000ull, words, 32, 0);
		if (n != 7 || words[1] != 0x60200000u || words[6] != 0x80000040u)
			return fail("p dma");
		n = r5_stage_p_build(R5_P_COHER, 0x50000ull, words, 32, 0);
		if (n != 3 || words[1] != 0x7bu)
			return fail("p coher");
		n = r5_stage_p_build(R5_P_DISPATCH, 0x50000ull, words, 32, 0);
		if (n != 20 || words[n - 5] != 0xc0031502u || words[n - 4] != 1u)
			return fail("p dispatch");
		if (R5_P_SHADER != 0xbf810000u)
			return fail("p shader");
	}

	{
		int errors = 0;
		int once = 0;
		int lines = 0;
		int i;
		for (i = 0; i < 3; i++) {
			if (r5_log_allow(0, 0, R5_LOG_DEBUG, &errors, &once, 0))
				lines++;
		}
		if (lines != 0)
			return fail("debug off");
		lines = 0;
		for (i = 0; i < 3; i++) {
			if (r5_log_allow(1, 0, R5_LOG_DEBUG, &errors, &once, 0))
				lines++;
		}
		if (lines != 3)
			return fail("debug on");
		if (!r5_log_allow(0, 0, R5_LOG_ERROR, &errors, &once, 0))
			return fail("error");
		errors = 32;
		if (r5_log_allow(0, 0, R5_LOG_ERROR, &errors, &once, 0))
			return fail("error cap");
		if (!r5_log_allow(0, 0, R5_LOG_ONCE, &errors, &once, 0))
			return fail("once");
		if (r5_log_allow(0, 0, R5_LOG_ONCE, &errors, &once, 0))
			return fail("once again");
		if (r5_log_allow(0, 0, R5_LOG_TEST, &errors, &once, 0))
			return fail("test off");
		if (!r5_log_allow(0, 1, R5_LOG_TEST, &errors, &once, 0))
			return fail("test on");
		if (!r5_log_allow(0, 0, R5_LOG_ONCE, &errors, &once, R5_ONCE_RB + 1))
			return fail("once other");
		errors = 0;
		if (!r5_log_allow(0, 0, R5_LOG_DUMP, &errors, &once, -1))
			return fail("dump start");
		lines = 0;
		for (i = 0; i < 100; i++) {
			if (r5_log_allow(0, 0, R5_LOG_DUMP, &errors, &once, 0))
				lines++;
		}
		if (lines != 64)
			return fail("dump cap");
		if (errors != 1)
			return fail("dump event");
		if (!r5_log_allow(0, 0, R5_LOG_ERROR, &errors, &once, 0))
			return fail("fault after dump");
	}
	{
		char text[4000];
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/r5_gpu.cpp", "gpu_debug_enabled()",
			"gpu_error_line()", text, sizeof(text)) != 0)
			return fail("debug reader");
		if (strstr(text, "load_driver_settings") != NULL)
			return fail("debug reads every call");
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/r5_gpu.cpp", "r5_gpu_init()", "r5_gpu_uninit()",
			text, sizeof(text)) != 0)
			return fail("init");
		if (strstr(text, "gpu_cache_log_settings()") == NULL)
			return fail("settings once");
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/r5_gpu.cpp", "sSelftestOn < 0",
			"gpu_known_dw(", text, sizeof(text)) != 0)
			return fail("selftest reader");
		if (strstr(text, "load_driver_settings") != NULL)
			return fail("selftest reads every call");
		if (snippet("distro/graphics/libdrm-r5/r5_translate.c",
			"R5_GPU_BO_CREATE", "gem->out.handle", text, sizeof(text)) != 0)
			return fail("bo domain line");
		if (strstr(text, "r5_debug_on()") == NULL
			|| strstr(text, "kernel bo domain") == NULL)
			return fail("bo domain ungated");
	}
	if (!r5_gpu_node_on(1, 1, 1))
		return fail("node on");
	if (r5_gpu_node_on(0, 0, 0) || r5_gpu_node_on(1, 1, 0)
		|| r5_gpu_node_on(1, 0, 1) || r5_gpu_node_on(0, 1, 1)
		|| r5_gpu_node_on(1, 0, 0) || r5_gpu_node_on(0, 1, 0)
		|| r5_gpu_node_on(0, 0, 1))
		return fail("node off");
	{
		char text[2000];
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/driver.cpp", "load_driver_settings(\"radeon_hd\")",
			"sR5Published[2] = NULL", text, sizeof(text)) != 0)
			return fail("node publish");
		if (strstr(text, "gfx9_kernel") == NULL
			|| strstr(text, "gfx9_irq") == NULL
			|| strstr(text, "gfx9_gart") == NULL
			|| strstr(text, "r5_gpu_node_on") == NULL
			|| strstr(text, "gpu/radeon_hd/0") == NULL)
			return fail("node keys");
	}

	if (r5_gpu_carveout_base(0x810u) != 0x810000000ull)
		return fail("carveout base");
	if (r5_gpu_carveout_base(0x01000810u) != 0x810000000ull)
		return fail("carveout mask");
	{
		char text[2500];
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/gfx9_k12.cpp", "map_pool(", "return true;",
			text, sizeof(text)) != 0)
			return fail("bar pool");
		if (strstr(text, "vram_window_phys") == NULL)
			return fail("bar window");
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/gfx9_k12.cpp", "map_carveout(", "return true;",
			text, sizeof(text)) != 0)
			return fail("carveout map");
		if (strstr(text, "r5_gpu_carveout_base") == NULL
			|| strstr(text, "B_WRITE_COMBINING_MEMORY") == NULL)
			return fail("carveout wc");
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/gfx9_k12.cpp", "vm_page_alloc(", "return 1;",
			text, sizeof(text)) != 0)
			return fail("vm page");
		if (strstr(text, "r5_gfx9_vram_cpu") == NULL
			|| strstr(text, "kernel_window") != NULL)
			return fail("vm page window");
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/r5_gpu.cpp", "gpu_stage_hdp(", "gpu_stage_r1_word(",
			text, sizeof(text)) != 0)
			return fail("hdp probe");
		if (strstr(text, "r5_gfx9_vram_bar") == NULL
			|| strstr(text, "r5_gfx9_vram_cpu") == NULL
			|| strstr(text, "sfence") == NULL)
			return fail("hdp paths");
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/gfx9_k12.cpp", "r5_gfx9_vmid1_flush()",
			"vmid1 fail tlb", text, sizeof(text)) != 0)
			return fail("vmid1 flush");
		if (strstr(text, "const uint32 eng = 17;") == NULL
			|| strstr(text, "eng = 0") != NULL)
			return fail("vmid1 engine");
		if (snippet("distro/graphics/src/add-ons/kernel/drivers/graphics/"
			"radeon_hd/r5_gpu.cpp", "r5_gfx9_map_vram(bo->vramOffset",
			"bo->kernel = kernel", text, sizeof(text)) != 0)
			return fail("bo map");
	}
	{
		uint32_t words[40];
		uint32_t n = r5_cs_vm_switch(words, 40, 1, 0x812c18001ull,
			0x9000ull, 7);
		if (n != 26 || n != r5_cs_vm_switch_words())
			return fail("vm switch words");
		if (words[0] != r5_cs_packet3(0x3c, 5) || words[1] != 0x113u
			|| words[4] != 7u || words[6] != 4u)
			return fail("vm switch pipe");
		/* gmc_v9_0_emit_flush_gpu_tlb, written by gfx_v9_0_ring_emit_wreg.
		 * WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM is 0x40100000.
		 * VMID 1 is context 1: 0x2000 + 0x08ed and the next register.
		 * Engine 0 is 0x2000 + 0x08a3 and 0x2000 + 0x08b5. */
		if (words[7] != r5_cs_packet3(0x37, 3) || words[8] != 0x40100000u
			|| words[9] != 0x28edu || words[11] != 0x12c18001u
			|| words[12] != r5_cs_packet3(0x37, 3) || words[13] != 0x40100000u
			|| words[14] != 0x28eeu || words[16] != 0x8u)
			return fail("vm switch ptb");
		/* gfx_v9_0_ring_emit_reg_write_reg_wait, me_fw_write_wait.
		 * VMID 1 request is bit 1 plus the five invalidate bits. */
		if (words[17] != r5_cs_packet3(0x3c, 5) || words[18] != 0x143u
			|| words[19] != 0x28a3u || words[20] != 0x28b5u
			|| words[21] != 0x007c0002u || words[22] != 0x2u
			|| words[23] != 0x20u)
			return fail("vm switch inv");
		if (words[24] != r5_cs_packet3(0x42, 0) || words[25] != 0)
			return fail("vm switch pfp");
		if (r5_cs_vm_switch(0, 0, 0, 0x812c18001ull, 0x9000ull, 7) != 0)
			return fail("vm switch vmid");
		if (r5_cs_vm_switch(words, 40, 1, 0, 0x9000ull, 7) != 0)
			return fail("vm switch pd");
		const char* tlbOrder[] = {
			"r5_gfx9_vmid1_map(uint64 va, uint64 pte)",
			"r5_gfx9_vmid1_touch()"
		};
		const char* ringOrder[] = {
			"r5_gfx9_user_submit(",
			"r5_cs_vm_switch(",
			"r5_gfx9_vmid1_flushed()"
		};
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/gfx9_k12.cpp", tlbOrder, 2) != 0)
			return 1;
		if (source_order(
				"distro/graphics/src/add-ons/accelerants/radeon_hd/common/"
				"gfx9_start.cpp", ringOrder, 3) != 0)
			return 1;
	}
	r5_cs_ring_ctx ringCtx;
	memset(&ringCtx, 0, sizeof(ringCtx));
	r5_ctx_counter handed;
	memset(&handed, 0, sizeof(handed));
	uint64_t first = r5_ctx_handout(&handed);
	uint64_t again = r5_ctx_handout(&handed);
	if (first == 0 || again == 0 || again == first
		|| r5_ctx_handout(0) != 0)
		return fail("context counter");
	if (r5_cs_ctx_switch(&ringCtx, first) == 0
		|| r5_cs_cntx(1, 0) != kR5CsCntxLoad)
		return fail("first context load");
	r5_cs_ctx_note(&ringCtx, first);
	if (r5_cs_ctx_switch(&ringCtx, first) != 0
		|| r5_cs_cntx(0, 0) != kR5CsCntxKeep)
		return fail("same context keep");
	if (r5_cs_ctx_switch(&ringCtx, again) == 0
		|| r5_cs_cntx(1, 0) != kR5CsCntxLoad)
		return fail("recreated context load");
	r5_cs_ctx_note(&ringCtx, again);
	if (r5_cs_ctx_switch(&ringCtx, 0) == 0)
		return fail("kernel context");
	if (r5_cs_cntx(1, kR5PreamblePresent) != 0x91018003u)
		return fail("preamble load ce");
	if (r5_cs_cntx(0, kR5PreambleFirst) != 0x90000000u)
		return fail("preamble first");
	if (r5_cs_cntx(1, kR5PreamblePresent | kR5PreambleFirst) != 0x91018003u)
		return fail("preamble switch wins");
	if (r5_cs_skip_preamble_ib(kR5IbPreamble, 0, 0) == 0)
		return fail("skip repeated preamble");
	if (r5_cs_skip_preamble_ib(kR5IbPreamble, 1, 0) != 0)
		return fail("keep preamble on switch");
	if (r5_cs_skip_preamble_ib(kR5IbPreamble, 0, kR5PreambleFirst) != 0)
		return fail("keep first preamble");
	if (r5_cs_skip_preamble_ib(0, 0, 0) != 0)
		return fail("keep plain ib");
	if (r5_cs_ib_va(0x00001000u, 0x8001u) != 0xffff800100001000ull)
		return fail("ib va sign");
	if (r5_cs_ib_va(0x00001000u, 0x0001u) != 0x000100001000ull)
		return fail("ib va low");
	#define R5_GC9_WAS(name, was) \
		if ((uint32_t)(name) != (uint32_t)(was)) return fail(#name);
	#include "r5-gc9-regs-check.inc"
	#undef R5_GC9_WAS
	if (r5_cs_gc_reg(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32) != 0x28ebu)
		return fail("ptb packet address");
	{
		uint32_t fence[16];
		uint32_t n = r5_cs_flush_fence(fence, 16, 0, 0x401040ull, 5);
		uint32_t skipped[64];
		uint64_t oneIb = 0x1000;
		uint32_t oneDw = 4;
		uint32_t oneVm = 1;
		uint32_t plain = r5_cs_user_body(skipped, 64, &oneIb, &oneDw, &oneVm,
			1, 0x66d000ull, 6, 0x401040ull, kR5CsCntxKeep, 1);
		if (n != 12u || fence[0] != r5_cs_packet3(0x49, 6)
			|| fence[1] != kR5CsEvent || fence[2] != kR5CsRel32Int
			|| fence[3] != 0x401040u || fence[5] != 5u
			|| fence[8] != r5_cs_packet3(0x8b, 0)
			|| fence[10] != r5_cs_packet3(0x8b, 0))
			return fail("flush fence");
		if (plain + 4u != r5_cs_user_words(1)
			|| skipped[0] != r5_cs_packet3(0x28, 1))
			return fail("flush fence opens the frame");
	}
	{
		uint64_t page = 0x80000000ull;
		uint64_t slot = r5_cs_cond_exec_mc(page);
		uint32_t pkt[8];
		uint32_t n = r5_cs_cond_exec(pkt, 8, 0, slot);
		r5_cs_cond_exec_patch(pkt, 4, 33);
		if (kR5CondExecOffset < kR5UserIrqOffset + 4u
			|| (kR5CondExecOffset & 3u) != 0
			|| slot != page + kR5CondExecOffset)
			return fail("cond exec slot");
		if (n != 5u || pkt[0] != r5_cs_packet3(0x22, 3)
			|| pkt[1] != (uint32_t)slot || pkt[2] != (uint32_t)(slot >> 32)
			|| pkt[4] != 33u)
			return fail("cond exec");
		const char* condOrder[] = {
			"kR5CondExecOffset / 4u] = 1u",
			"r5_gfx9_user_submit(",
			"r5_cs_cond_exec_mc("
		};
		if (source_order(
				"distro/graphics/src/add-ons/accelerants/radeon_hd/common/"
				"gfx9_start.cpp", condOrder, 3) != 0)
			return 1;
		uint32_t cond[8];
		if (r5_csa_vaddr(r5_gfx9_vm_size().max_pfn) != 0xffffffffffe00000ull
			|| kR5CsaBytes != 128ull * 1024ull
			|| kR5VaReservedCsa != (2ull << 20))
			return fail("csa vaddr");
		if (r5_vm_va_fits(r5_csa_vaddr(r5_gfx9_vm_size().max_pfn), 4096) != 0)
			return fail("csa reserved");
		if (r5_vm_va_fits(0xffffffffffbfd000ull, 4096) == 0)
			return fail("high page");
		if (r5_vm_va_fits(R5_GPU_VA_OFFSET, 4096) == 0
			|| r5_vm_va_fits(R5_GPU_HIGH_VA_MAX - 4096ull, 4096) == 0)
			return fail("low va");
		uint32_t meta[16];
		if (r5_cs_ce_meta(meta, 16, 0, r5_csa_vaddr(r5_gfx9_vm_size().max_pfn)) != 14u
			|| meta[0] != 0xc00c3700u || meta[1] != 0x80100800u
			|| meta[2] != 0xffe00000u || meta[3] != 0xffffffffu
			|| meta[4] != 0u || meta[13] != 0u)
			return fail("ce meta");
		if (r5_cs_cond_exec(cond, 8, 0, 0x401080ull) != 5u
			|| cond[0] != 0xc0032200u || cond[1] != 0x00401080u
			|| cond[2] != 0u || cond[3] != 0u)
			return fail("cond packet");
		r5_cs_cond_exec_patch(cond, 4, 0x22u);
		if (cond[4] != 0x22u)
			return fail("cond flush skip");
		r5_cs_cond_exec_patch(cond, 4, 0x2du);
		if (cond[4] != 0x2du)
			return fail("cond job skip");
		uint32_t opened[64];
		uint64_t oneIb = 0x1000;
		uint32_t oneDw = 4;
		uint32_t oneVm = 1;
		uint32_t whole = r5_cs_user_body(opened, 64, &oneIb, &oneDw, &oneVm,
			1, 0x66d000ull, 3, 0x401040ull, kR5CsCntxKeep, 0, 0);
		uint32_t inner = r5_cs_user_body(opened, 64, &oneIb, &oneDw, &oneVm,
			1, 0x66d000ull, 3, 0x401040ull, kR5CsCntxKeep, 1, 1);
		if (whole < 6u || inner != whole - 6u || opened[0] == 0xc0008b00u
			|| opened[inner - 2u] == 0xc0008b00u)
			return fail("cond outside switches");
		const char* twoCond[] = {
			"r5_cs_cond_exec(packets, cap, switched,",
			"r5_cs_vm_switch(",
			"r5_cs_cond_exec_patch(packets, slotA,",
			"r5_cs_cond_exec(packets, cap, switched,",
			"r5_cs_user_body(",
			"r5_cs_cond_exec_patch(packets, slotB,"
		};
		if (source_order(
				"distro/graphics/src/add-ons/accelerants/radeon_hd/common/"
				"gfx9_start.cpp", twoCond, 6) != 0)
			return 1;
	}
	{
		struct r5_gds empty;
		struct r5_gds zero;
		memset(&empty, 0, sizeof(empty));
		memset(&zero, 0, sizeof(zero));
		zero.valid = 1;
		struct r5_gds sized = zero;
		sized.gds_size = 1;
		uint32_t pkt[24];
		uint32_t n = r5_cs_gds_switch(pkt, 24, 0, 1, &zero);
		if (r5_gds_switch_needed(&empty, &zero) == 0
			|| r5_gds_switch_needed(&zero, &zero) != 0
			|| r5_gds_switch_needed(&zero, &sized) == 0)
			return fail("gds needed");
		if (n != 20u || pkt[0] != r5_cs_packet3(0x37, 3) || pkt[1] != 0u
			|| pkt[2] != r5_cs_gc_reg(mmGDS_VMID0_BASE) + 2u
			|| pkt[4] != 0u
			|| pkt[7] != r5_cs_gc_reg(mmGDS_VMID0_SIZE) + 2u
			|| pkt[12] != r5_cs_gc_reg(mmGDS_GWS_VMID0) + 1u
			|| pkt[17] != r5_cs_gc_reg(mmGDS_OA_VMID0) + 1u
			|| pkt[19] != 0u)
			return fail("gds switch");
	}
	{
		struct r5_pasid_ida ida;
		memset(&ida, 0, sizeof(ida));
		int first = r5_pasid_alloc(&ida, 16);
		int second = r5_pasid_alloc(&ida, 16);
		r5_pasid_free(&ida, (uint32_t)first);
		int again = r5_pasid_alloc(&ida, 16);
		uint32_t pkt[8];
		uint32_t n = r5_cs_pasid_map(pkt, 8, 0, 1, (uint32_t)first);
		if (r5_pasid_alloc(0, 16) != -22 || r5_pasid_alloc(&ida, 0) != -22)
			return fail("pasid bits");
		if (first != 32768 || second != 32769 || again != first)
			return fail("pasid alloc");
		if (n != 5u || pkt[0] != r5_cs_packet3(0x37, 3)
			|| pkt[1] != 0x40100000u
			|| pkt[2] != r5_cs_oss_reg(mmIH_VMID_0_LUT) + 1u
			|| pkt[4] != (uint32_t)first)
			return fail("pasid map");
		if (r5_pasid_map_due(0, 0, (uint32_t)first) != 0
			|| r5_pasid_map_due(0, (uint32_t)first, (uint32_t)second) != 0)
			return fail("pasid map off");
		/* Settings off, no VM flush: r5_gfx9_user_submit writes this
		 * body and nothing in front of it. That is the frame from
		 * before the PASID LUT write. Two clients must match it
		 * dword for dword, and neither frame contains the LUT. */
		const uint32_t beforeN8[] = {
			0x8bu, 0x8bu, 0x28u, 0x90u, 0x3fu, 0x90u, 0x49u, 0x49u, 0x8bu
		};
		uint32_t bodyA[64];
		uint32_t bodyB[64];
		uint64_t ib = 0x1000;
		uint32_t dw = 4;
		uint32_t vm = 1;
		uint32_t na = r5_cs_user_body(bodyA, 64, &ib, &dw, &vm, 1,
			0x66d000ull, 3, 0x401040ull, kR5CsCntxKeep, 0);
		uint32_t nb = r5_cs_user_body(bodyB, 64, &ib, &dw, &vm, 1,
			0x66d000ull, 3, 0x401040ull, kR5CsCntxKeep, 0);
		if (na == 0 || na != nb || na != r5_cs_user_words(1))
			return fail("two clients, settings off");
		uint32_t lut = r5_cs_oss_reg(mmIH_VMID_0_LUT);
		uint32_t seen = 0;
		for (uint32_t i = 0; i < na; i++) {
			if (bodyA[i] != bodyB[i])
				return fail("two clients differ");
			if (bodyA[i] == lut || bodyA[i] == lut + 1u)
				return fail("lut in the off frame");
			if ((bodyA[i] >> 30) == 3u) {
				uint32_t op = (bodyA[i] >> 8) & 0xffu;
				if (seen >= 9u || op != beforeN8[seen])
					return fail("before N8");
				seen++;
			}
		}
		if (seen != 9u)
			return fail("before N8 count");
		const char* pasidOrder[] = {
			"int inBlock = flush || gds;",
			"int pasidMap = inBlock",
			"r5_cs_pasid_map("
		};
		if (source_order(
				"distro/graphics/src/add-ons/accelerants/radeon_hd/common/"
				"gfx9_start.cpp", pasidOrder, 3) != 0)
			return 1;
	}
	if (r5_gfx9_inv_engines() != 18u)
		return fail("inv engines");
	if (r5_gfx9_inv_range_lo(0) != 0x8c7u
		|| r5_gfx9_inv_range_hi(0) != 0x8c8u)
		return fail("inv eng0");
	if (r5_gfx9_inv_range_lo(1)
		!= r5_gfx9_inv_range_lo(0) + R5_GC9_ENG_ADDR_DISTANCE)
		return fail("inv step");
	if (r5_gfx9_inv_range_lo(17) != 0x8c7u + 17u * R5_GC9_ENG_ADDR_DISTANCE
		|| r5_gfx9_inv_range_hi(17)
			!= mmVM_INVALIDATE_ENG0_ADDR_RANGE_HI32
				+ 17u * R5_GC9_ENG_ADDR_DISTANCE)
		return fail("inv eng17");
	if (r5_gfx9_inv_range_lo_value() != 0xffffffffu
		|| r5_gfx9_inv_range_hi_value() != 0x1fu)
		return fail("inv values");
	{
		struct r5_vm_size size = r5_vm_adjust(256u * 1024u, 3u, 48u, 16u);
		uint32_t cntl = r5_gfx9_vmid_cntl(&size, 0);
		uint32_t depthBits = VM_CONTEXT0_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT
			- VM_CONTEXT0_CNTL__PAGE_TABLE_DEPTH__SHIFT;
		if (size.num_level != 3u || size.block_size != 9u)
			return fail("vm levels");
		if (size.max_pfn != (256ull * 1024ull) << 18)
			return fail("vm pfn");
		if (((cntl >> VM_CONTEXT0_CNTL__PAGE_TABLE_DEPTH__SHIFT)
				& ((1u << depthBits) - 1u)) != 3u)
			return fail("vmid depth");
		if (((cntl >> VM_CONTEXT0_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT)
				& 0xfu) != 0u)
			return fail("vmid block");
		if ((cntl & (1u << VM_CONTEXT0_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT))
				!= 0)
			return fail("vmid retry");
		if (r5_gfx9_sh_mem_config() != 0x1018u
			|| r5_gfx9_sh_mem_bases() != 0x20001000u)
			return fail("sh mem");
		const char* vmidOrder[] = {
			"if (!sVmids || sInfo == NULL)",
			"for (i = 0; i <= 14u; i++)"
		};
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/gfx9_k12.cpp", vmidOrder, 2) != 0)
			return 1;
	}
	{
		struct r5_agp_aperture agp = r5_gfx9_agp_aperture(0, 0x3fffffffull, 1);
		if (agp.bot != 0xffffffu || agp.top != 0u || agp.low != 0u
			|| agp.high != ((0x3fffffffull >> 18) + 1ull))
			return fail("agp default");
		struct r5_agp_aperture plain = r5_gfx9_agp_aperture(0, 0x3fffffffull, 0);
		if (plain.high != (uint32_t)(0x3fffffffull >> 18))
			return fail("agp no plus");
		uint64_t fbA = 0;
		uint64_t fbB = 0;
		uint64_t fbC = 0;
		uint64_t fbD = 0;
		r5_gfx9_fb_range(0x00abcdefu, 256ull * 1024ull * 1024ull, &fbA, &fbB);
		r5_gfx9_fb_range(0x00abcdefu, 256ull * 1024ull * 1024ull, &fbC, &fbD);
		if (fbA != fbC || fbB != fbD || fbA != (0xabcdefull << 24)
			|| fbB != fbA + 256ull * 1024ull * 1024ull - 1ull
			|| MC_VM_FB_LOCATION_BASE__FB_BASE_MASK != 0x00ffffffu)
			return fail("fb range");
		{
			const char* plusOrder[] = {
				"gDeviceInfo[found]->aperturePlus = "
					"kSupportedDevices[type].aperturePlus",
				"info.shared_info->aperturePlus = info.aperturePlus",
				"r5_gfx9_set_aperture_plus(info.aperturePlus)",
				"sAperturePlus);",
				"r5_gfx9_agp_aperture(fbStart, fbEnd, sInfo->aperturePlus)"
			};
			if (source_order(
					"distro/graphics/src/add-ons/kernel/drivers/graphics/"
					"radeon_hd/driver.cpp", plusOrder, 1) != 0)
				return 1;
			if (source_order(
					"distro/graphics/src/add-ons/kernel/drivers/graphics/"
					"radeon_hd/radeon_hd.cpp", plusOrder + 1, 1) != 0)
				return 1;
			if (source_order(
					"distro/graphics/src/add-ons/kernel/drivers/graphics/"
					"radeon_hd/gfx9_k12.cpp", plusOrder + 2, 1) != 0)
				return 1;
			if (source_order(
					"distro/graphics/src/add-ons/accelerants/radeon_hd/common/"
					"gfx9_start.cpp", plusOrder + 3, 1) != 0)
				return 1;
			if (source_order(
					"distro/graphics/src/add-ons/kernel/drivers/graphics/"
					"radeon_hd/gfx9_k12.cpp", plusOrder + 4, 1) != 0)
				return 1;
		}
		struct r5_agp_aperture above = r5_gfx9_agp_aperture(
			0x1000000000000ull, 0x1000000000000ull, 1);
		if (above.low != (uint32_t)(0xffffffffffffull >> 18))
			return fail("agp min");
	}
	if (mmMC_VM_AGP_BASE != 0x984u
		|| mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32 != 0x855u
		|| mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32 != 0x856u
		|| VM_L2_PROTECTION_FAULT_CNTL2__ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY__SHIFT
			!= 0x12u)
		return fail("hub linux regs");
	{
		const char* hubOrder[] = {
			"if (!sHubLinux || sInfo == NULL)",
			"gc_byte(mmMC_VM_AGP_BASE), 0",
			"gc_byte(mmMC_VM_AGP_BOT), agp.bot",
			"gc_byte(mmMC_VM_AGP_TOP), agp.top",
			"mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32), 0",
			"mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32), 0"
		};
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/gfx9_k12.cpp", hubOrder, 6) != 0)
			return 1;
		const char* rangeOrder[] = {
			"r5_gfx9_fb_range(location, sVramBytes, &frame, &fbEnd);",
			"r5_gfx9_fb_range(location,"
		};
		if (source_order(
				"distro/graphics/src/add-ons/accelerants/radeon_hd/common/"
				"gfx9_start.cpp", rangeOrder, 1) != 0)
			return 1;
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/gfx9_k12.cpp", rangeOrder + 1, 1) != 0)
			return 1;
	}
	{
		const char* invOrder[] = {
			"kwrite(sInfo, gc_byte(mmVM_CONTEXT1_CNTL), cntl);",
			"r5_gfx9_inv_range_program();"
		};
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/gfx9_k12.cpp", invOrder, 2) != 0)
			return 1;
	}
	{
		struct r5_vmid_mgr mgr;
		int flush = 0;
		uint32_t i;
		uint32_t got[16];
		r5_vmid_init(&mgr);
		for (i = 1; i <= 15u; i++) {
			got[i] = r5_vmid_reserve(&mgr, i, 0, 1, &flush);
			if (got[i] == 0 || got[i] > 15u)
				return fail("vmid grab");
			r5_vmid_arm(&mgr, 10);
		}
		if (got[1] == got[2])
			return fail("two vmids");
		if (r5_vmid_reserve(&mgr, 16, 0, 1, &flush) != 0)
			return fail("sixteenth vmid");
		r5_vmid_release(&mgr, 1, 10);
		flush = 0;
		uint32_t again = r5_vmid_reserve(&mgr, 20, 10, 1, &flush);
		if (again == 0 || flush == 0)
			return fail("closed file");
		r5_vmid_cancel(&mgr);
	}
	{
		struct r5_defer held[2];
		uint64_t page = 0;
		memset(held, 0, sizeof(held));
		if (r5_defer_add(held, 2, 0x1000) == 0)
			return fail("defer add");
		if (r5_defer_take(held, 2, 5, &page) != 0)
			return fail("defer early");
		r5_defer_stamp(held, 2, 8);
		if (r5_defer_take(held, 2, 7, &page) != 0)
			return fail("defer before fence");
		if (r5_defer_take(held, 2, 8, &page) == 0 || page != 0x1000ull)
			return fail("defer after fence");
	}
	if (R5_GPU_DEBUG_SWITCH != R5_GPU_OP_BASE + 25u)
		return fail("debug switch opcode");
	if (R5_GPU_STAGE_R1 != R5_GPU_OP_BASE + 26u)
		return fail("stage r1 opcode");
	if (R5_GPU_LOG_REGS != R5_GPU_OP_BASE + 27u)
		return fail("log regs opcode");
	if (R5_GPU_SWITCH_CE_META != 256u)
		return fail("ce switch bit");
	{
		uint32_t words[8];
		uint64_t x = 0xffff800000100000ull;
		if (r5_r1_write(words, 8, x, 0x11111111u) != 6u
			|| words[0] != r5_cs_packet3(0x37, 4) || words[4] != 0x11111111u)
			return fail("r1 write");
		if (r5_r1_release(words, 8, x, 0x22222222u) != 8u
			|| words[0] != r5_cs_packet3(0x49, 6) || words[5] != 0x22222222u)
			return fail("r1 release");
		const char* r1Order[] = {
			"gpu_stage_r1_line(\"r1a gtt\"",
			"gpu_stage_r1_line(\"r1a vram\"",
			"gpu_stage_r1_line(\"r1b gtt\"",
			"gpu_stage_r1_line(\"r1b vram\"",
			"r1c:",
			"gpu_stage_r1_line(\"r1c gtt\"",
			"gpu_stage_r1_line(\"r1c vram\"",
			"gpu_stage_r1_line(\"r1 free a\""
		};
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/r5_gpu.cpp", r1Order, 8) != 0)
			return 1;
		if (R5_GPU_R1_AB != 1u || R5_GPU_R1_C != 2u)
			return fail("r1 which");
		if (r5_vm_va_fits(0xffff800000100000ull, 0x2000) == 0
			|| r5_vm_va_fits(0xffff800000300000ull, 0x2000) == 0
			|| r5_vm_va_fits(0xffff800000500000ull, 0x2000) == 0)
			return fail("r1 round va");
		char r1Body[8000];
		if (snippet(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/r5_gpu.cpp",
				"gpu_stage_r1(radeon_info*", "gpu_stage_u(",
				r1Body, sizeof(r1Body)) != 0)
			return 1;
		if (strstr(r1Body, "0xffff800000100000") != NULL)
			return fail("r1 shared va");
		char r1Line[1500];
		if (snippet(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/r5_gpu.cpp",
				"gpu_stage_r1_line(const char*", "gpu_stage_r1(radeon_info*",
				r1Line, sizeof(r1Line)) != 0)
			return 1;
		if (strstr(r1Line, "t %lld") == NULL
			|| strstr(r1Line, "inv %d") == NULL
			|| strstr(r1Line, "ce %d") == NULL)
			return fail("r1 line");
	}
	{
		const char* ceBoot[] = {
			"R5_GPU_SWITCH_CE_META",
			"gfx9_ce_meta is a boot setting",
			"r5_gfx9_switch_registers("
		};
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/r5_gpu.cpp", ceBoot, 3) != 0)
			return 1;
	}
	{
		const char* switchOrder[] = {
			"Idle first. The flush is after the registers move.",
			"r5_gfx9_switch_registers(",
			"if (!r5_gfx9_vmid1_flush())",
			"r5_gfx9_pasid_map_on()"
		};
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/r5_gpu.cpp", switchOrder, 4) != 0)
			return 1;
		const char* apertureOrder[] = {
			"mmMC_VM_SYSTEM_APERTURE_LOW_ADDR));",
			"sHubProgram(frame, fbEnd, aperLow, aperHigh);",
			"mmMC_VM_MX_L1_TLB_CNTL"
		};
		if (source_order(
				"distro/graphics/src/add-ons/accelerants/radeon_hd/common/"
				"gfx9_start.cpp", apertureOrder, 3) != 0)
			return 1;
		const char* storeOrder[] = {
			"r5_gfx9_k2_store();",
			"r5_gfx9_inv_range_read(\"k2\")",
			"r5_gfx9_vmid_restore();",
			"r5_gfx9_vmid_baseline();",
			"r5_gfx9_inv_range_program();"
		};
		if (source_order(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/"
				"radeon_hd/gfx9_k12.cpp", storeOrder, 5) != 0)
			return 1;
	}
	printf("ok gpu api %u.%u walk\n", R5_GPU_API_MAJOR, R5_GPU_API_MINOR);
	return 0;
}
