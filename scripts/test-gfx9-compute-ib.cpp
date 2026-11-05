/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Host check for the GFX9 compute indirect buffer. The expected dwords are
 * written out here. They are not produced by calling the builder.
 */
#include "gfx9_compute_ib.h"

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
	return 0;
}
