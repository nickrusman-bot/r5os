/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Host check for the GFX9 compute indirect buffer. The expected dwords are
 * written out here. They are not produced by calling the builder.
 */
#include "gfx9_compute_ib.h"
#include "gfx9_check.h"

#include <stdio.h>
#include <string.h>

static const uint64_t kShaderMc = 0xf400800000ull;
static const uint64_t kBufferMc = 0xf400900000ull;
static const uint64_t kTs0Mc = 0xf4007ea480ull;
static const uint64_t kTs1Mc = 0xf4007ea4c0ull;
static const uint32_t kRsrc1 = 0x000c0000u;
static const uint32_t kRsrc2 = 0x00000084u;

static const uint32_t kExpectOne[] = {
	0xc0055800u, 0x28c40000u, 0xffffffffu, 0x00ffffffu,
	0x00000000u, 0x00000000u, 0x0000000au,
	0xc0017900u, 0x00000200u, 0xe0000000u,
	0xc0027600u, 0x0000020cu, 0xf4008000u, 0x00000000u,
	0xc0027600u, 0x00000212u, 0x000c0000u, 0x00000084u,
	0xc0017600u, 0x00000215u, 0x00000000u,
	0xc0017600u, 0x00000216u, 0xffffffffu,
	0xc0017600u, 0x00000217u, 0xffffffffu,
	0xc0017600u, 0x00000219u, 0xffffffffu,
	0xc0017600u, 0x0000021au, 0xffffffffu,
	0xc0017600u, 0x00000218u, 0x00000000u,
	0xc0037600u, 0x00000204u, 0x00000000u, 0x00000000u, 0x00000000u,
	0xc0037600u, 0x00000207u, 0x00000040u, 0x00000001u, 0x00000001u,
	0xc0027600u, 0x00000240u, 0x00900000u, 0x000000f4u,
	0xc0064900u, 0x00238514u, 0x60000000u, 0x007ea480u, 0x000000f4u,
	0x00000000u, 0x00000000u, 0x00000000u,
	0xc0031500u, 0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u,
	0xc0004600u, 0x00000407u,
	0xc0064900u, 0x00238514u, 0x60000000u, 0x007ea4c0u, 0x000000f4u,
	0x00000000u, 0x00000000u, 0x00000000u
};

static const uint32_t kExpectMany[] = {
	0xc0055800u, 0x28c40000u, 0xffffffffu, 0x00ffffffu,
	0x00000000u, 0x00000000u, 0x0000000au,
	0xc0017900u, 0x00000200u, 0xe0000000u,
	0xc0027600u, 0x0000020cu, 0xf4008000u, 0x00000000u,
	0xc0027600u, 0x00000212u, 0x000c0000u, 0x00000084u,
	0xc0017600u, 0x00000215u, 0x00000000u,
	0xc0017600u, 0x00000216u, 0xffffffffu,
	0xc0017600u, 0x00000217u, 0xffffffffu,
	0xc0017600u, 0x00000219u, 0xffffffffu,
	0xc0017600u, 0x0000021au, 0xffffffffu,
	0xc0017600u, 0x00000218u, 0x00000000u,
	0xc0037600u, 0x00000204u, 0x00000000u, 0x00000000u, 0x00000000u,
	0xc0037600u, 0x00000207u, 0x00000040u, 0x00000001u, 0x00000001u,
	0xc0027600u, 0x00000240u, 0x00900000u, 0x000000f4u,
	0xc0064900u, 0x00238514u, 0x60000000u, 0x007ea480u, 0x000000f4u,
	0x00000000u, 0x00000000u, 0x00000000u,
	0xc0031500u, 0x00004000u, 0x00000001u, 0x00000001u, 0x00000001u,
	0xc0004600u, 0x00000407u,
	0xc0064900u, 0x00238514u, 0x60000000u, 0x007ea4c0u, 0x000000f4u,
	0x00000000u, 0x00000000u, 0x00000000u
};

static const uint32_t kShIndex[] = {
	0x0e0cu, 0x0e12u, 0x0e15u, 0x0e16u, 0x0e17u, 0x0e19u, 0x0e1au,
	0x0e18u, 0x0e04u, 0x0e07u, 0x0e40u
};


static int
fail(const char* what)
{
	fprintf(stderr, "fail %s\n", what);
	return 1;
}


static int
compare(const char* name, const uint32_t* got, uint32_t gotCount,
	const uint32_t* expect, uint32_t expectCount)
{
	uint32_t i;
	if (gotCount != expectCount)
		return fail(name);
	for (i = 0; i < gotCount; i++) {
		if (got[i] != expect[i]) {
			fprintf(stderr, "fail %s dword %u got %08x want %08x\n",
				name, i, got[i], expect[i]);
			return 1;
		}
	}
	return 0;
}


static int
check_offsets(const uint32_t* ib, uint32_t count)
{
	uint32_t i = 0;
	uint32_t seen = 0;
	while (i < count) {
		uint32_t header = ib[i];
		uint32_t opcode = (header >> 8) & 0xffu;
		uint32_t body = ((header >> 16) & 0x3fffu) + 1u;
		if (i + body >= count)
			return fail("packet length");
		if (opcode == 0x76u) {
			uint32_t offset;
			if (seen >= 11)
				return fail("set sh count");
			offset = ib[i + 1];
			if (offset != kShIndex[seen] - 0x0c00u)
				return fail("set sh offset");
			if (offset != (0x2000u + kShIndex[seen]) - 0x2c00u)
				return fail("set sh start formula");
			seen++;
		}
		i += 1u + body;
	}
	if (seen != 11)
		return fail("set sh seen");
	return 0;
}


static int
check_case(const char* name, uint32_t groups, const uint32_t* expect,
	uint32_t expectCount)
{
	uint32_t ib[1024];
	uint32_t count = r5_gfx9_ib_compute(ib, kShaderMc, kRsrc1, kRsrc2,
		kBufferMc, groups, kTs0Mc, kTs1Mc);
	if (count == 0 || count > 1024)
		return fail("ib size");
	if (compare(name, ib, count, expect, expectCount) != 0)
		return 1;
	if (check_offsets(ib, count) != 0)
		return 1;
	if ((kShaderMc & 255ull) != 0)
		return fail("shader align");
	if (ib[12] != (uint32_t)(kShaderMc >> 8))
		return fail("pgm lo");
	if (ib[13] != (uint32_t)(kShaderMc >> 40))
		return fail("pgm hi");
	return 0;
}


int
main(void)
{
	uint32_t submit[16];
	uint32_t submitCount;
	uint32_t emptyCount;
	uint32_t ib[1024];
	if (check_case("one", 1, kExpectOne,
			(uint32_t)(sizeof(kExpectOne) / sizeof(kExpectOne[0]))) != 0)
		return 1;
	if (check_case("many", 16384, kExpectMany,
			(uint32_t)(sizeof(kExpectMany) / sizeof(kExpectMany[0]))) != 0)
		return 1;
	emptyCount = r5_gfx9_ib_empty(ib, kShaderMc, kRsrc1, kRsrc2,
		kBufferMc, kTs0Mc, kTs1Mc);
	if (compare("empty", ib, emptyCount, kExpectOne,
			(uint32_t)(sizeof(kExpectOne) / sizeof(kExpectOne[0]))) != 0)
		return 1;
	submitCount = r5_gfx9_submit(submit, 0xf400810000ull, 73, 0xf4007ea440ull, 2);
	if (submitCount != 12)
		return fail("submit count");
	if (submit[0] != 0xc0023f00u || submit[1] != 0x00810000u
		|| submit[2] != 0x000000f4u || submit[3] != ((1u << 23) | 73u))
		return fail("indirect");
	if (submit[4] != 0xc0064900u || submit[6] != 0x20000000u
		|| submit[7] != 0x007ea440u || submit[9] != 2u)
		return fail("release");
	printf("ok dwords %u submit %u\n",
		(unsigned)(sizeof(kExpectOne) / sizeof(kExpectOne[0])), submitCount);

	const uint64_t kPhys = 0x0000000123450000ull;
	const uint64_t kFlags = R5_GFX9_PTE_VALID | R5_GFX9_PTE_SYSTEM
		| R5_GFX9_PTE_SNOOPED | R5_GFX9_PTE_EXECUTABLE | R5_GFX9_PTE_READABLE
		| R5_GFX9_PTE_WRITEABLE;
	const uint64_t kExpectPte = 0x0600000123450077ull;
	uint64_t pte = r5_gfx9_gart_pte(kPhys, kFlags);
	if (pte != kExpectPte)
		return fail("pte value");
	if ((pte & 1ull) == 0 || (pte & 2ull) == 0 || (pte & 4ull) == 0)
		return fail("pte low");
	if ((pte & (1ull << 4)) == 0)
		return fail("pte exec");
	if ((pte & (1ull << 5)) == 0 || (pte & (1ull << 6)) == 0)
		return fail("pte rw");
	if (((pte >> 57) & 3ull) != 3ull)
		return fail("pte mtype");
	if ((pte & ~0xfffull) != (kPhys | (3ull << 57)))
		return fail("pte phys");
	const uint64_t kTablePa = (0x810ull << 24) + 0x800000ull;
	if (!r5_gfx9_pde_aligned(kTablePa) || r5_gfx9_pde_aligned(kTablePa + 32ull))
		return fail("pde align");
	if (r5_gfx9_page_table_base(0x810u, 0x800000ull) != (kTablePa | 1ull))
		return fail("ptb");
	if (r5_gfx9_tlb_req(0, 0) != 0x007c0001u)
		return fail("tlb req");
	if ((r5_gfx9_tlb_req(0, 3) & 0x00030000u) != 0x00030000u)
		return fail("flush type");
	if ((r5_gfx9_tlb_req(0, 4) & 0x00070000u) != 0x00040000u)
		return fail("flush width");
	r5_gfx9_fault decoded = r5_gfx9_fault_decode(0x00040d5bu);
	if (decoded.more != 1 || decoded.walker != 5 || decoded.permission != 5
		|| decoded.mapping != 1 || decoded.cid != 6u || decoded.rw != 1)
		return fail("fault decode");
	if (r5_gfx9_fault_address(0x0f3f0000u, 0) != 0xf3f0000000ull)
		return fail("fault addr");
	const uint64_t kWrap = 914ull + 9ull * 12ull;
	if (kWrap != 1022ull)
		return fail("wrap base");
	const uint32_t kWrapIndex[12] = {
		1022, 1023, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
	};
	for (uint32_t i = 0; i < 12; i++) {
		if (r5_gfx9_ring_index(kWrap, i, 1024) != kWrapIndex[i])
			return fail("wrap index");
	}
	if (r5_gfx9_ring_space(1024, kWrap, kWrap) < 12)
		return fail("wrap space");
	if (r5_gfx9_ring_space(1024, 914, 914) < 12)
		return fail("first space");
	if (r5_gfx9_ring_space(1024, kWrap, 0) != 1)
		return fail("full space");

	uint64_t gartStart = 0;
	uint64_t gartEnd = 0;
	const uint64_t kFb = 0xf400000000ull;
	const uint64_t kAperStart = 0x3d0000ull << 18;
	const uint64_t kAperEnd = 0x3d0800ull << 18;
	uint64_t agpStart = 0;
	uint64_t agpEnd = 0;
	if (r5_gfx9_agp_window(1u, 0u, &agpStart, &agpEnd))
		return fail("agp off");
	if (!r5_gfx9_agp_window(0u, 0u, &agpStart, &agpEnd))
		return fail("agp on");
	if (agpStart != 0 || agpEnd != 0x1000000ull)
		return fail("agp 16m");
	if (!r5_gfx9_gart_range(kFb, kAperStart, kAperEnd, 0, 0, 0,
			&gartStart, &gartEnd))
		return fail("gart range");
	if (gartStart != 0xf3f0000000ull || gartEnd != 0xf400000000ull)
		return fail("gart place");
	if (!(gartEnd <= kAperStart || gartStart >= kAperEnd))
		return fail("gart overlap");
	if (gartEnd > kFb)
		return fail("gart fb");
	if (!r5_gfx9_gart_range(kFb, kAperStart, kAperEnd, 1, agpStart, agpEnd,
			&gartStart, &gartEnd))
		return fail("gart agp clear");
	if (r5_gfx9_gart_range(kFb, kAperStart, kAperEnd, 1, gartStart, gartEnd,
			&gartStart, &gartEnd))
		return fail("gart agp hit");

	uint8_t raw[32];
	memset(raw, 0, sizeof(raw));
	raw[0] = 0x14;
	raw[1] = 181;
	raw[2] = 0;
	r5_gfx9_ih_entry entry = r5_gfx9_ih_parse(raw);
	if (entry.client != 0x14u || entry.source != 181u || entry.ring != 0)
		return fail("ih parse");
	printf("ok pte gart ih\n");
	return 0;
}
