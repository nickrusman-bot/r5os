/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "distro/rest/src/system/runtime_loader/r5_tpoff.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int
main()
{
	const uint64_t threadPointer = 0x100000;
	const uint64_t templateBase = 0x1000;
	const uint64_t symbolOffset = 0x20;
	const int64_t addend = 8;

	const uint64_t offset = r5_tpoff_offset(templateBase, symbolOffset, addend);
	if (offset != 0x1028)
		return 1;
	if (threadPointer + offset != 0x101028)
		return 2;

	unsigned char small[8];
	memset(small, 0xff, sizeof(small));
	const uint32_t smallStored = (uint32_t)offset;
	memcpy(small, &smallStored, sizeof(smallStored));
	uint32_t smallRead = 0;
	memcpy(&smallRead, small, sizeof(smallRead));
	if (smallRead != 0x1028)
		return 3;
	if (small[4] != 0xff || small[5] != 0xff || small[6] != 0xff || small[7] != 0xff)
		return 4;

	const uint64_t wide = r5_tpoff_offset(0x100000000ull, 0x120, 0);
	unsigned char mem[8];
	memset(mem, 0xff, sizeof(mem));
	const uint32_t stored = (uint32_t)wide;
	memcpy(mem, &stored, sizeof(stored));
	uint32_t low = 0;
	memcpy(&low, mem, sizeof(low));
	if (low != 0x120)
		return 5;
	if (mem[4] != 0xff || mem[5] != 0xff || mem[6] != 0xff || mem[7] != 0xff)
		return 6;

	printf("ok tpoff offset\n");
	return 0;
}
