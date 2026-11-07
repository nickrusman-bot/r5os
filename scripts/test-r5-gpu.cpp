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
#include "r5_vmid.h"
#include "r5_limit.h"
#include "r5_span.h"
#include "r5_vm.h"
#include "r5_wait.h"
#include "r5_stage_p.h"
#include "r5_log.h"
#include "gfx9_check.h"
#include "../distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_backend.h"

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
		0x66d000ull, 3, 0x401040ull, kR5CsCntxKeep);
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
		0xabc000ull, 9, 0xabd000ull, kR5CsCntxKeep);
	uint32_t load[64];
	uint32_t loadN = r5_cs_user_body(load, 64, otherIb, otherDw, ibVm, 1,
		0xabc000ull, 9, 0xabd000ull, kR5CsCntxLoad);
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
	char text[20000];
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
			"distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp",
			"r5_gpu_eop()", "gpu_counter(", text, sizeof(text)) != 0)
		return 1;
	if (strstr(text, "gpu_seen()") == NULL)
		return fail("retire fence");
	if (snippet(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp",
			"gpu_counter(", "gpu_wait_value(", text, sizeof(text)) != 0)
		return 1;
	if (strstr(text, "return gpu_seen()") == NULL)
		return fail("wait fence");
	if (snippet(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
			"amd_poll_retired", "amd_fault_decode", text, sizeof(text)) != 0)
		return 1;
	if (strstr(text, "*seen = r5_gfx9_user_seen()") == NULL)
		return fail("retire fence");
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


static int
read_file(const char* path, char** out)
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
	*out = text;
	return 0;
}


static int
source_order_two(const char* first, const char* second, const char** marks,
	int count)
{
	char* a = NULL;
	char* b = NULL;
	if (read_file(first, &a) != 0)
		return 1;
	if (read_file(second, &b) != 0) {
		free(a);
		return 1;
	}
	size_t na = strlen(a);
	size_t nb = strlen(b);
	char* text = (char*)malloc(na + nb + 2);
	if (text == NULL) {
		free(a);
		free(b);
		return fail("order");
	}
	memcpy(text, a, na);
	text[na] = '\n';
	memcpy(text + na + 1, b, nb);
	text[na + nb + 1] = 0;
	free(a);
	free(b);
	const char* cursor = text;
	for (int i = 0; i < count; i++) {
		const char* found = strstr(cursor, marks[i]);
		if (found == NULL) {
			fprintf(stderr, "fail order %s\n", marks[i]);
			free(text);
			return 1;
		}
		cursor = found + strlen(marks[i]);
	}
	free(text);
	return 0;
}


/* The core calls the backend through gR5GpuOps. It does not include an
 * AMD header or call an AMD function by name. */
static int
core_boundary()
{
	char* text = NULL;
	if (read_file(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp",
			&text) != 0)
		return 1;
	const char* banned[] = {
		"gfx9_", "r5_gfx9", "r5_amd", "amdgpu", "kR5Gfx", "R5_GFX9_",
		"kGcSegment", "atombios", "r5_gpu_amd_info"
	};
	for (unsigned i = 0; i < sizeof(banned) / sizeof(banned[0]); i++) {
		if (strstr(text, banned[i]) != NULL) {
			fprintf(stderr, "fail core boundary %s\n", banned[i]);
			free(text);
			return 1;
		}
	}
	free(text);
	return 0;
}


static uint64_t gVmWords[64][512];
static uint32_t gVmPages;


static int
vm_alloc(void*, uint64_t* physical, uint64_t** cpu)
{
	if (gVmPages >= 64)
		return 0;
	*physical = ((uint64_t)gVmPages + 1ull) << 21;
	*cpu = gVmWords[gVmPages];
	memset(*cpu, 0, 512 * sizeof(uint64_t));
	gVmPages++;
	return 1;
}


/* Two page tables, one VA. Closing one leaves the other. An unmap on
 * VMID n invalidates n. The same owner flushes when its tlb sequence
 * changes. A finished reserve does not block the other program. */
static int
two_spaces()
{
	char* text = NULL;
	const char* submit;
	const char* end;
	const char* nulls;
	const char* reserve;
	r5_vm_page pagesA[16];
	r5_vm_page pagesB[16];
	r5_vm a;
	r5_vm b;
	uint64_t va = 0xffff800100000000ull;
	uint64_t got = 0;
	r5_vmid_mgr mgr;
	int flush = 0;
	uint32_t first;
	uint32_t second;
	uint32_t again;
	if (read_file(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
			&text) != 0)
		return fail("two read");
	if (strstr(text, "sPhase") != NULL || strstr(text, "sBound") != NULL) {
		free(text);
		return fail("two bind");
	}
	submit = strstr(text, "amd_submit(");
	end = submit != NULL ? strstr(submit, "amd_poll_retired") : NULL;
	nulls = submit != NULL ? strstr(submit, "sequence == NULL") : NULL;
	reserve = submit != NULL ? strstr(submit, "as_reserve") : NULL;
	if (submit == NULL || end == NULL || nulls == NULL || nulls > end
		|| reserve == NULL || reserve < nulls || reserve > end
		|| strstr(submit, "sh_mem") < end) {
		free(text);
		return fail("two submit");
	}
	free(text);
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	a.pages = pagesA;
	b.pages = pagesB;
	a.capacity = 16;
	b.capacity = 16;
	a.alloc = vm_alloc;
	b.alloc = vm_alloc;
	gVmPages = 0;
	if (!r5_vm_map_page(&a, va, 0x111ull) || !r5_vm_map_page(&b, va, 0x223ull))
		return fail("two map");
	if (!r5_vm_lookup(&a, va, &got) || got != 0x111ull)
		return fail("two a");
	if (!r5_vm_lookup(&b, va, &got) || got != 0x223ull)
		return fail("two b");
	a.used = 0;
	if (!r5_vm_lookup(&b, va, &got) || got != 0x223ull)
		return fail("two close");
	if (r5_gfx9_tlb_req(7, 0) == r5_gfx9_tlb_req(1, 0))
		return fail("two inv");
	r5_vmid_init(&mgr);
	first = r5_vmid_reserve(&mgr, 2, 0, 1, &flush);
	if (first == 0)
		return fail("two own");
	r5_vmid_arm(&mgr, 5);
	again = r5_vmid_reserve(&mgr, 2, 0, 1, &flush);
	if (again != first || flush != 0)
		return fail("two keep");
	again = r5_vmid_reserve(&mgr, 2, 0, 2, &flush);
	if (again != first || flush != 1)
		return fail("two tlb");
	r5_vmid_init(&mgr);
	first = r5_vmid_reserve(&mgr, 1, 0, 0, &flush);
	r5_vmid_arm(&mgr, 1);
	second = r5_vmid_reserve(&mgr, 2, 0, 0, &flush);
	if (second == 0 || second == first)
		return fail("two other");
	r5_vmid_arm(&mgr, 2);
	again = r5_vmid_reserve(&mgr, 1, 0, 0, &flush);
	if (again != first || flush != 0)
		return fail("two alt");
	return 0;
}


static uint64_t* gGibStore;
static uint32_t gGibN;


static int
gib_alloc(void*, uint64_t* physical, uint64_t** cpu)
{
	if (gGibN >= 600)
		return 0;
	*physical = ((uint64_t)gGibN + 1ull) << 21;
	*cpu = gGibStore + (size_t)gGibN * 512;
	memset(*cpu, 0, 512 * sizeof(uint64_t));
	gGibN++;
	return 1;
}


/* A contiguous 1 GiB map needs more than 256 table pages. */
static int
one_gib()
{
	r5_vm_page pages[600];
	r5_vm vm;
	uint64_t va = 0x100000000ull;
	uint64_t bytes = 1ull << 30;
	uint64_t page;
	uint64_t got = 0;
	char* text = NULL;
	const char* fn;
	const char* brace;
	const char* end;
	gGibStore = (uint64_t*)calloc(600ull * 512ull, sizeof(uint64_t));
	if (gGibStore == NULL)
		return fail("gib alloc");
	memset(&vm, 0, sizeof(vm));
	memset(pages, 0, sizeof(pages));
	vm.pages = pages;
	vm.capacity = 600;
	vm.alloc = gib_alloc;
	gGibN = 0;
	for (page = 0; page < bytes; page += 4096) {
		if (!r5_vm_map_page(&vm, va + page, 0x1001ull + page)) {
			free(gGibStore);
			return fail("gib map");
		}
	}
	if (!r5_vm_lookup(&vm, va, &got) || got != 0x1001ull) {
		free(gGibStore);
		return fail("gib first");
	}
	if (!r5_vm_lookup(&vm, va + bytes - 4096, &got)
		|| got != 0x1001ull + bytes - 4096) {
		free(gGibStore);
		return fail("gib last");
	}
	free(gGibStore);
	if (read_file(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/gfx9_k12.cpp",
			&text) != 0)
		return fail("gib read");
	if (strstr(text, "vm_page_num_pages") != NULL
		|| strstr(text, "get_system_info") == NULL
		|| strstr(text, "R5_IH_CLIENT_UTCL2") == NULL
		|| strstr(text, "entry.client == 0x1au") != NULL) {
		free(text);
		return fail("gib kernel");
	}
	free(text);
	if (read_file(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp",
			&text) != 0)
		return fail("gib core");
	fn = strstr(text, "gpu_render_enabled()");
	brace = fn != NULL ? strchr(fn, '{') : NULL;
	end = brace != NULL ? strchr(brace, '}') : NULL;
	if (fn == NULL || end == NULL || strstr(brace, "return true") == NULL
		|| strstr(brace, "return true") > end) {
		free(text);
		return fail("gib render");
	}
	free(text);
	if (read_file("distro/graphics/libdrm-r5/r5_translate.c", &text) != 0)
		return fail("gib drm");
	if (strstr(text, "0x80000000") == NULL || strstr(text, "ENOMEM") == NULL) {
		free(text);
		return fail("gib enomem");
	}
	free(text);
	return 0;
}


/* B_OK is 0. Every op returns that on success. A fake backend that
 * returns 0 must pass `!= 0`. One that returns 1 must fail it. `!op()`
 * reads 0 as failure, which is the bug this test rejects. */
static int op_fill(void*, void*, uint32_t) { return 0; }
static int op_reg(void*, uint32_t, uint32_t*) { return 0; }
static int op_alloc(uint64_t, uint64_t*) { return 0; }
static int op_free(uint64_t) { return 0; }
static int op_type(int, uint64_t, uint32_t) { return 0; }
static int op_pte(uint64_t, uint32_t, int, int, uint64_t* pte)
{
	if (pte == 0)
		return 1;
	*pte = 1;
	return 0;
}
static int op_map(void*, uint64_t, uint64_t) { return 0; }
static int op_unmap(void*, uint64_t, uint64_t) { return 0; }
static int op_prt(void*, uint64_t, uint64_t) { return 0; }
static int op_flush(void*) { return 0; }
static int op_prepare(void) { return 0; }
static int op_lookup(void*, uint64_t) { return 0; }
static int op_sh(uint32_t) { return 0; }
static int op_sh_one(uint32_t) { return 1; }
static int op_submit(void*, const uint64_t*, const uint32_t*, uint32_t, uint64_t*)
{
	return 0;
}
static int op_seen(uint64_t* seen)
{
	if (seen == 0)
		return 1;
	*seen = 1;
	return 0;
}
static int op_ih(const uint32_t*, uint32_t, uint32_t*, uint64_t*) { return 0; }
static int op_fault(uint32_t, uint32_t, uint64_t*) { return 0; }
static int op_dump(const char*) { return 0; }
static int op_stage_p(void*, void*) { return 0; }
static int op_stage_u(void*) { return 0; }
static int op_as_open(void** as)
{
	if (as == 0)
		return 1;
	*as = 0;
	return 0;
}
static int op_as_close(void*, uint64_t) { return 0; }
static int op_bind(void*) { return 0; }
static int op_limits(uint64_t* vram, uint64_t* gtt)
{
	if (vram == 0 || gtt == 0)
		return 1;
	*vram = 1;
	*gtt = 1;
	return 0;
}
static int op_retired(uint64_t) { return 0; }
static int op_room(uint32_t) { return 0; }
static int op_lost(void*) { return 0; }
static int op_hold(void*) { return 0; }
static int op_drop(void*) { return 0; }
static int op_vendor(uint32_t* bytes, uint32_t* version)
{
	if (bytes != 0)
		*bytes = 8;
	if (version != 0)
		*version = 1;
	return 0;
}
static int op_counts(void*, uint32_t* pages, uint32_t* vmid, uint32_t* pagesAll,
	uint32_t* vmidsAll)
{
	if (pages != 0)
		*pages = 1;
	if (vmid != 0)
		*vmid = 0;
	if (pagesAll != 0)
		*pagesAll = 2;
	if (vmidsAll != 0)
		*vmidsAll = 0;
	return 0;
}


static int
ops_status()
{
	r5_gpu_backend_ops ops = {
		op_fill, op_reg, op_alloc, op_free, op_type, op_pte,
		op_map, op_unmap, op_prt, op_flush, op_prepare, op_lookup,
		op_sh, op_submit, op_seen, op_ih, op_fault, op_dump,
		op_stage_p, op_stage_u, op_as_open, op_as_close, op_bind,
		op_limits, op_retired, op_room, op_lost, op_hold, op_drop, op_counts,
		op_vendor
	};
	uint32_t reg = 0;
	uint64_t off = 0;
	uint64_t pte = 0;
	uint64_t seen = 0;
	uint32_t source = 0;
	uint64_t address = 0;
	if (ops.fill_vendor_info(0, 0, 0) != 0)
		return fail("op fill");
	if (ops.read_reg(0, 0, &reg) != 0)
		return fail("op reg");
	if (ops.vram_alloc(0, &off) != 0 || ops.vram_free(0) != 0)
		return fail("op vram");
	if (ops.set_cpu_type(0, 0, 0) != 0)
		return fail("op type");
	if (ops.encode_pte(0, 0, 0, 0, &pte) != 0 || pte == 0)
		return fail("op pte");
	if (ops.vm_map(0, 0, 0) != 0 || ops.vm_unmap(0, 0, 0) != 0
		|| ops.vm_prt(0, 0, 0) != 0 || ops.tlb_flush(0) != 0)
		return fail("op map");
	if (ops.vmid_prepare() != 0 || ops.vmid_lookup(0, 0) != 0)
		return fail("op vmid");
	if (ops.sh_mem(1) != 0)
		return fail("op sh_mem");
	/* Success is 0, so `!status` is true. That reading is rejected. */
	if ((!ops.sh_mem(1)) == 0)
		return fail("op sh_mem bang");
	ops.sh_mem = op_sh_one;
	if (ops.sh_mem(1) == 0)
		return fail("op sh_mem one");
	{
		void* as = 0;
		uint64_t vram = 0;
		uint64_t gtt = 0;
		if (ops.address_open(&as) != 0 || ops.bind(as) != 0
			|| ops.limits(&vram, &gtt) != 0 || vram == 0 || gtt == 0
			|| ops.note_retired(0) != 0 || ops.address_close(as, 0) != 0)
			return fail("op address");
	}
	if (ops.submit(0, 0, 0, 0, 0) != 0 || ops.wait_room(1) != 0
		|| ops.space_lost(0) != 0 || ops.space_hold(0) != 0
		|| ops.space_drop(0) != 0)
		return fail("op submit");
	if (r5_gpu_ops_ready(&ops) != 1)
		return fail("ops ready");
	ops.submit = 0;
	if (r5_gpu_ops_ready(&ops) != 0)
		return fail("ops ready missing");
	{
		char* openText = NULL;
		const char* openFn;
		const char* locked;
		const char* opened;
		const char* unlocked;
		if (read_file(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp",
				&openText) != 0)
			return fail("open read");
		openFn = strstr(openText, "gpu_open(");
		locked = openFn != NULL ? strstr(openFn, "gpu_lock()") : NULL;
		opened = locked != NULL ? strstr(locked, "address_open") : NULL;
		unlocked = opened != NULL ? strstr(opened, "gpu_unlock()") : NULL;
		free(openText);
		if (locked == NULL || opened == NULL || unlocked == NULL)
			return fail("open lock");
	}
	{
		uint64_t first = 0;
		uint64_t last = 1;
		if (r5_ib_span(~0ull - 7ull, 4, &first, &last))
			return fail("ib wrap");
		if (r5_ib_span(0xffff800000001004ull, 0x100000u, &first, &last))
			return fail("ib cap");
		if (!r5_ib_span(0xffff800000001004ull, 8, &first, &last)
			|| first != 0xffff800000001000ull || last != first)
			return fail("ib last");
	}
	if (r5_space_free_ok(1, 4, 9) || r5_space_free_ok(0, 9, 4))
		return fail("space held");
	if (!r5_space_free_ok(0, 4, 4) || !r5_space_free_ok(0, 4, 9))
		return fail("space free");
	if (ops.poll_retired(&seen) != 0 || seen == 0)
		return fail("op seen");
	if (ops.ih_decode(0, 0, &source, &address) != 0
		|| ops.fault_decode(0, 0, &address) != 0 || ops.dump(0) != 0)
		return fail("op decode");
	if (ops.stage_p(0, 0) != 0 || ops.stage_u(0) != 0)
		return fail("op stage");
	char* text = NULL;
	if (read_file(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp",
			&text) != 0)
		return 1;
	if (strstr(text, "!gR5GpuOps.") != NULL) {
		free(text);
		return fail("op bang call");
	}
	free(text);
	if (read_file(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
			&text) != 0)
		return 1;
	if (strstr(text, ".vendor_size = amd_vendor_size") == NULL)
		return fail("ops designated");
	if (strstr(text, "!gR5GpuOps.") != NULL || strstr(text, "? 1 : 0;") != NULL) {
		free(text);
		return fail("op backend bang");
	}
	if (strstr(text, "? B_OK : B_ERROR") == NULL
		|| strstr(text, "B_ENTRY_NOT_FOUND") == NULL) {
		free(text);
		return fail("op backend status");
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
	{
		r5_amd_device dev;
		uint32_t fwv[32];
		uint32_t fwf[32];
		memset(fwv, 0, sizeof(fwv));
		memset(fwf, 0, sizeof(fwf));
		fwv[4] = 0xa7u;
		fwf[4] = 54u;
		/* Stick 18 syslog: CC_GC_SHADER_ARRAY_CONFIG ff080000, user 0.
		 * rb num 2 mask 3, which is no backend disabled. */
		r5_amd_device_fill(&dev, 0x15e7u, 0, 0, 0, 0, 0xff080000u, 0,
			fwv, fwf);
		if (dev.num_shader_engines != 1 || dev.num_cu_per_sh != 8)
			return fail("passport se");
		if (dev.cu_active_number != 7 || dev.cu_bitmap != 0xf7u)
			return fail("passport cu");
		if (dev.num_rb_pipes != 2 || dev.enabled_rb_pipes_mask != 3)
			return fail("passport rb");
		if (dev.max_engine_clock != 1800000u
			|| dev.max_memory_clock != 1333000u
			|| dev.vram_type != 11u || dev.vram_bit_width != 128u)
			return fail("passport clocks");
		if (dev.gpu_counter_freq != 100000u || dev.family != 142u)
			return fail("passport freq");
		if (dev.external_rev != 0xa1u || dev.hw_major != 9 || dev.hw_minor != 3)
			return fail("passport rev");
		if (dev.gpu_info_loaded != 0 || dev.atom_read != 0)
			return fail("passport handover");
		if (dev.fw_ver[4] != 0xa7u || dev.fw_feature[4] != 54u)
			return fail("passport fw");
		{
			uint8_t hdr[44];
			uint32_t ver = 0;
			uint32_t feat = 0;
			memset(hdr, 0, sizeof(hdr));
			hdr[16] = 0x11;
			hdr[17] = 0x22;
			hdr[32] = 0x33;
			hdr[33] = 0x44;
			if (r5_amd_ucode_versions(hdr, sizeof(hdr), &ver, &feat) != 1
				|| ver != 0x2211u || feat != 0x4433u
				|| r5_amd_ucode_versions(hdr, 8, &ver, &feat) != 0)
				return fail("ucode header");
		}
		if (!r5_amd_gfx9_cp(0x15e7u) || r5_amd_gfx9_cp(0x164cu))
			return fail("passport cp");
		if (r5_amd_row_find(0x15e7u) == NULL
			|| r5_amd_row_find(0x15e7u)->gen != r5_amd_row_find(0x1636u)->gen
			|| r5_amd_row_find(0x1638u)->gen != r5_amd_row_find(0x164cu)->gen
			|| strcmp(r5_amd_row_find(0x1636u)->gen->name, "GC 9.3 Renoir") != 0)
			return fail("chip gen");
		if (r5_amd_fw_stem(0) != NULL || r5_amd_fw_stem(0x1234u) != NULL
			|| strcmp(r5_amd_fw_stem(0x1636u), "renoir") != 0
			|| strcmp(r5_amd_fw_stem(0x15e7u), "green_sardine") != 0)
			return fail("passport stem");
		if (r5_amd_need_reset(0x164cu) == 0 || r5_amd_need_reset(0x15ddu) != 0)
			return fail("passport reset");
		r5_amd_device other;
		r5_amd_device_fill(&other, 0x1636u, 1, 0, 0, 0, 0, 0, NULL, NULL);
		if (other.gpu_counter_freq != 100000u || other.external_rev != 0x92u)
			return fail("passport 1636");
		char text[4096];
		r5_amd_passport_format(&dev, text, (int)sizeof(text));
		if (strstr(text, "num_shader_engines 1 (ip discovery not parsed") == NULL)
			return fail("passport line");
		if (strstr(text, "atom_read 0 (device table until ATOM integratedsysteminfo") == NULL)
			return fail("passport atom");
		if (strstr(text, "firmwareinfo default_sclk") == NULL
			|| strstr(text, "SPLL reference") == NULL)
			return fail("passport clocks source");
	}
	if (system("python3 scripts/check-gpu-hardcode.py") != 0)
		return fail("hardcode check");
	if (core_boundary() != 0)
		return 1;
	if (ops_status() != 0)
		return 1;
	{
		r5_gpu_backend_ops empty;
		memset(&empty, 0, sizeof(empty));
		if (r5_gpu_ops_ready(&empty) != 0 || r5_gpu_ops_ready(NULL) != 0)
			return fail("ops ready empty");
	}
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
		if (source_order_two(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp",
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
		irqMc, kR5CsCntxKeep);
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
	if (R5_GPU_BO_VRAM_CLEARED != 8u)
		return fail("vram cleared flag");
	{
		uint8_t mem[0x2000];
		memset(mem, 0xab, sizeof(mem));
		memset(spans, 0, sizeof(spans));
		spans[0].live = 1;
		spans[0].used = 0;
		spans[0].offset = 0x2000;
		spans[0].bytes = 0x2000;
		spans[0].cpu = mem;
		taken = 0;
		if (!r5_span_take(spans, 4, 0x1000, &taken) || taken != 0x2000)
			return fail("vram take");
		if (mem[0] != 0 || mem[0xfff] != 0 || mem[0x1000] != 0xab)
			return fail("vram zero");
	}
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
		"\"vulkaninfo\"",
		"VulkanTeapot (sam)",
		"\"VulkanTeapot\"",
		"stage_selftest(out)",
		"highVa, true"
	};
	const char* stageOrder[] = {
		"gpu_stage_p(",
		"gpu_stage_u(",
		"\"u-self\"",
		"\"u2a-n\"",
		"\"u2a-f\"",
		"\"u2b-n\"",
		"\"u4b-pair\"",
		"\"u-top\"",
		"\"u-radv\"",
		"\"u-257\"",
		"\"u-pdb1\"",
		"\"u-pdb0\"",
		"\"u-low0\"",
		"\"u4b\""
	};
	/* Linux 6.12 gfxhub_v1_0_init_tlb_regs, from the same prior 0x2501.
	 * Mask 0x3879 is the fields it writes. ECO stays. */
	static_assert(((0x2501u & ~0x3879u) | 0x3859u) == 0x3d59u, "l1 tlb");
	static_assert((0x1018u ^ 0x18u) == 0x1000u, "sh mem retry");
	static_assert((0x007ffe87u ^ 0x007ffe07u) == 0x80u, "context retry");
	if (source_order(
			"distro/graphics/src/bin/r5-gpu-selftest/r5-gpu-selftest.cpp",
			selftestOrder, 6) != 0)
		return 1;
	if (source_order(
			"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
			stageOrder, 14) != 0)
		return 1;
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
	}

	{
		struct r5_vmid_mgr mgr;
		uint32_t got[21];
		uint32_t i;
		int flush = 0;
		int shared;
		uint32_t owners[16];
		uint32_t nown;
		memset(got, 0, sizeof(got));
		r5_vmid_init(&mgr);
		for (i = 1; i <= 15u; i++) {
			uint32_t v = r5_vmid_reserve(&mgr, i, 0, 0, &flush);
			if (v == 0 || flush != 1)
				return fail("vmid first");
			r5_vmid_arm(&mgr, 1);
			got[i] = v;
		}
		if (r5_vmid_reserve(&mgr, 16, 0, 0, &flush) != 0)
			return fail("vmid 16th");
		{
			uint32_t again = r5_vmid_reserve(&mgr, 1, 0, 0, &flush);
			if (again != got[1] || flush != 0)
				return fail("vmid keep");
			r5_vmid_arm(&mgr, 2);
		}
		for (i = 2; i <= 15u; i++)
			r5_vmid_release(&mgr, i, 1);
		if (mgr.slot[got[1]].owner != 1)
			return fail("vmid held");
		for (i = 16; i <= 20u; i++) {
			uint32_t v = r5_vmid_reserve(&mgr, i, 1, 0, &flush);
			if (v == 0 || flush != 1 || v == got[1])
				return fail("vmid reuse");
			r5_vmid_arm(&mgr, 1);
			got[i] = v;
		}
		memset(owners, 0, sizeof(owners));
		nown = 0;
		for (i = 1; i <= 15u; i++) {
			uint32_t owner = mgr.slot[i].owner;
			uint32_t n;
			if (owner == 0)
				continue;
			for (n = 0; n < nown; n++) {
				if (owners[n] == owner)
					return fail("vmid shared");
			}
			owners[nown++] = owner;
		}
		shared = 0;
		for (i = 1; i <= 20u; i++) {
			if (got[i] == 0)
				shared = 1;
		}
		if (shared || nown < 6)
			return fail("vmid count");
		r5_vmid_init(&mgr);
		if (r5_vmid_reserve(&mgr, 3, 0, 0, &flush) == 0)
			return fail("vmid cancel");
		r5_vmid_cancel(&mgr);
		if (mgr.hold != 0 || mgr.slot[1].owner != 0)
			return fail("vmid restored");
		if (r5_limit_vram(512ull << 20) != (256ull << 20))
			return fail("limit vram");
		if (r5_limit_gtt(8ull << 30) != (2ull << 30))
			return fail("limit gtt");
		if (r5_limit_allow(200ull << 20, 100ull << 20, 256ull << 20) != 0)
			return fail("limit over");
		if (r5_limit_allow(100ull << 20, 100ull << 20, 256ull << 20) != 1)
			return fail("limit fit");
		{
			uint32_t words[40];
			uint32_t n = r5_cs_vm_switch(words, 40, 2, 0x42u, 0x812c18001ull,
				0x9000ull, 7);
			uint32_t body[40];
			uint64_t ib = 0x1000ull;
			uint32_t dw = 8;
			uint32_t vm = 2;
			uint32_t bare;
			if (n != 36 || n != r5_cs_vm_switch_words()
				|| words[0] != r5_cs_packet3(0x3c, 5) || words[1] != 0x113u
				|| words[4] != 7u)
				return fail("vm switch");
			if (words[7] != 0xc0033700u || words[9] != r5_cs_oss_byte(2)
				|| words[11] != 0x42u)
				return fail("vm switch pasid");
			if (words[12] != 0xc0033700u || words[14] != r5_cs_gc_byte(0x08efu)
				|| words[16] != 0x12c18001u)
				return fail("vm switch pd");
			if (words[26] != r5_gfx9_tlb_req(2, 0) || words[28] != 0x103u
				|| words[31] != r5_gfx9_tlb_ack_mask(2))
				return fail("vm switch ack");
			if (words[34] != r5_cs_packet3(0x42, 0) || words[35] != 0)
				return fail("vm switch pfp");
			bare = r5_cs_user_body(body, 40, &ib, &dw, &vm, 1, 0x9000ull, 8,
				0x9010ull, kR5CsCntxKeep);
			if (bare != 33)
				return fail("vm switch body");
			if (r5_cs_vm_switch(0, 0, 0, 0, 0, 0, 0) != 0)
				return fail("vm switch vmid0");
			if (r5_cs_vm_switch(words, 40, 2, 0, 0x812c18001ull, 0x9000ull, 7) != 0)
				return fail("vm switch pasid0");
		}
		printf("ok vmid 20 15\n");
	}

	if (two_spaces() != 0)
		return 1;
	if (one_gib() != 0)
		return 1;
	{
		uint8_t raw[32];
		uint32_t* dword = (uint32_t*)raw;
		uint32_t owners[2] = { 2u, 1u };
		memset(raw, 0, sizeof(raw));
		dword[0] = R5_IH_CLIENT_UTCL2 | (1u << 24);
		if (!r5_gfx9_ih_gc_fault(raw)
			|| r5_gfx9_fault_owner((dword[0] >> 24) & 0xfu, owners, 2) != 1)
			return fail("ih utcl2");
		dword[0] = R5_IH_CLIENT_RESERVED | (1u << 24);
		if (r5_gfx9_ih_gc_fault(raw))
			return fail("ih reserved");
		dword[0] = R5_IH_CLIENT_UTCL2 | (2u << 24);
		if (r5_gfx9_fault_owner((dword[0] >> 24) & 0xfu, owners, 2) != 0)
			return fail("ih other file");
	}
	{
		r5_vm_page slotsA[8];
		r5_vm_page slotsB[8];
		static host_page pagesA[8];
		static host_page pagesB[8];
		static int nextA = 0;
		static int nextB = 0;
		nextA = 0;
		nextB = 0;
		auto allocA = [](void* cookie, uint64_t* physical, uint64_t** cpu) -> int {
			int* next = (int*)cookie;
			if (*next >= 8)
				return 0;
			host_page* page = &pagesA[*next];
			page->physical = 0x910000000ull + (uint64_t)(*next) * 4096ull;
			*physical = page->physical;
			*cpu = page->cpu;
			*next += 1;
			return 1;
		};
		auto allocB = [](void* cookie, uint64_t* physical, uint64_t** cpu) -> int {
			int* next = (int*)cookie;
			if (*next >= 8)
				return 0;
			host_page* page = &pagesB[*next];
			page->physical = 0x920000000ull + (uint64_t)(*next) * 4096ull;
			*physical = page->physical;
			*cpu = page->cpu;
			*next += 1;
			return 1;
		};
		r5_vm spaceA;
		r5_vm spaceB;
		memset(&spaceA, 0, sizeof(spaceA));
		memset(&spaceB, 0, sizeof(spaceB));
		memset(pagesA, 0, sizeof(pagesA));
		memset(pagesB, 0, sizeof(pagesB));
		spaceA.pages = slotsA;
		spaceB.pages = slotsB;
		spaceA.capacity = 8;
		spaceB.capacity = 8;
		spaceA.alloc = allocA;
		spaceB.alloc = allocB;
		spaceA.cookie = &nextA;
		spaceB.cookie = &nextB;
		const uint64_t sharedVa = 0xffff800000001000ull;
		const uint64_t leaf = 0x0600000123450071ull;
		uint64_t got = 0;
		if (!r5_vm_map_page(&spaceA, sharedVa, leaf)
			|| !r5_vm_map_page(&spaceB, sharedVa, leaf))
			return fail("space map");
		if (!r5_vm_map_page(&spaceA, sharedVa, 0))
			return fail("space clear");
		if (r5_vm_lookup(&spaceA, sharedVa, &got) != 0 || got != kR5VmPteEmpty)
			return fail("space cleared");
		if (!r5_vm_lookup(&spaceB, sharedVa, &got) || got != leaf)
			return fail("space other");
		char* core = NULL;
		if (read_file(
				"distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp",
				&core) != 0)
			return fail("space read");
		if (strstr(core, "sBo[b].space != space") == NULL
			|| strstr(core, "sPrt[i].space != space") == NULL
			|| strstr(core, "sPrt[i].space == space") == NULL
			|| strstr(core, "gpu_prt_release_space") == NULL) {
			free(core);
			return fail("space book");
		}
		free(core);
	}
	if (r5_gfx9_vmid_cntl() != 0x007ffe07u)
		return fail("vmid cntl");
	{
		r5_gfx9_vmid_ctx regs;
		uint32_t prev = 0;
		for (uint32_t vmid = 1; vmid <= 15; vmid++) {
			if (!r5_gfx9_vmid_ctx_regs(vmid, &regs))
				return fail("vmid ctx");
			if (regs.cntl != 0x0881u + (vmid - 1)
				|| regs.start_lo != 0x090du + (vmid - 1) * 2u
				|| regs.start_hi != regs.start_lo + 1u
				|| regs.end_lo != 0x092du + (vmid - 1) * 2u
				|| regs.end_hi != regs.end_lo + 1u
				|| regs.cntl == prev)
				return fail("vmid regs");
			prev = regs.cntl;
		}
		if (r5_gfx9_vmid_ctx_regs(0, &regs) || r5_gfx9_vmid_ctx_regs(16, &regs))
			return fail("vmid range");
	}
	{
		r5_gpu_counters same;
		memset(&same, 0, sizeof(same));
		same.bo_count = 3;
		same.pt_pages = 9;
		same.vmids = 1;
		r5_gpu_counters other = same;
		other.bo_count = 4;
		if (r5_gpu_counters_match(&same, &same) != 1
			|| r5_gpu_counters_match(&same, &other) != 0
			|| r5_gpu_counters_match(0, &same) != 0)
			return fail("counters");
	}
	{
		char* iso = NULL;
		if (read_file(
			"distro/graphics/src/bin/r5-gpu-isolation/r5-gpu-isolation.cpp",
			&iso) != 0)
			return fail("isolation file");
		if (strstr(iso, "COPY_DATA") == NULL || strstr(iso, "WRITE_DATA") == NULL
			|| strstr(iso, "owner unchanged") == NULL
			|| strstr(iso, "vm fault") == NULL || strstr(iso, "0x5a") == NULL) {
			free(iso);
			return fail("isolation");
		}
		free(iso);
	}
	{
		uint8_t bin[256];
		uint8_t* gc;
		r5_amd_device dev;
		char mismatch[160];
		uint16_t sum;
		uint32_t i;
		memset(bin, 0, sizeof(bin));
		bin[0] = 0x07;
		bin[1] = 0x14;
		bin[2] = 0x21;
		bin[3] = 0x28;
		bin[10] = 0;
		bin[11] = 1;
		gc = bin + 128;
		bin[20] = 128;
		gc[4] = 2;
		gc[12] = 1;
		gc[16] = 8;
		gc[20] = 1;
		gc[24] = 2;
		gc[28] = 4;
		gc[33] = 1;
		gc[40] = 32;
		gc[44] = 99;
		gc[52] = 1;
		bin[24] = 80;
		sum = 0;
		for (i = 0; i < 80; i++)
			sum = (uint16_t)(sum + gc[i]);
		bin[22] = (uint8_t)sum;
		bin[23] = (uint8_t)(sum >> 8);
		sum = 0;
		for (i = 10; i < sizeof(bin); i++)
			sum = (uint16_t)(sum + bin[i]);
		bin[8] = (uint8_t)sum;
		bin[9] = (uint8_t)(sum >> 8);
		memset(&dev, 0, sizeof(dev));
		dev.device_id = 0x1636;
		dev.known = 1;
		if (r5_amd_discovery_apply(&dev, bin, sizeof(bin), mismatch,
				(int)sizeof(mismatch)) != 1
			|| dev.num_shader_engines != 1 || dev.num_cu_per_sh != 8
			|| dev.gs_prim_buffer_depth != 99
			|| strstr(mismatch, "discovery mismatch") == NULL)
			return fail("discovery");
		bin[12] ^= 0xff;
		if (r5_amd_discovery_apply(&dev, bin, sizeof(bin), mismatch,
				(int)sizeof(mismatch)) != 0)
			return fail("discovery checksum");
	}
	if (r5_gpu_query_guilty(R5_GPU_CAP_LOST) != 0
		|| r5_gpu_query_guilty(R5_GPU_CAP_GUILTY) != 0
		|| r5_gpu_query_guilty(R5_GPU_CAP_LOST | R5_GPU_CAP_GUILTY) != 1)
		return fail("guilty");
	printf("ok gpu api %u.%u walk\n", R5_GPU_API_MAJOR, R5_GPU_API_MINOR);
	return 0;
}
