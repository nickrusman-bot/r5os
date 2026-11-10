/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Visible-VRAM bump. The PSP trusted-memory region, the discovery region
 * and the AtomBIOS firmware reservation stay out of every allocation
 * except the PSP claim itself.
 */

#ifndef R5_VRAM_POOL_H
#define R5_VRAM_POOL_H

#include <stdint.h>

/* GC 9.3 SETUP_TMR in this tree takes 4 MiB, aligned to 4 MiB, and the
 * stick recorded that page at window offset 0xc00000. A request of that
 * size and alignment may take the range. Any other request skips it.
 * Linux v6.12 amdgpu_discovery.c reserves DISCOVERY_TMR_OFFSET (64 KiB)
 * plus DISCOVERY_TMR_SIZE (10 KiB) at the top of real VRAM.
 */
#define R5_VRAM_PSP_TMR_OFFSET	0xc00000ull
#define R5_VRAM_PSP_TMR_BYTES	(4ull * 1024ull * 1024ull)
#define R5_VRAM_DISCOVERY_BYTES	((64ull + 10ull) * 1024ull)
#define R5_VRAM_HOLES		3

typedef struct {
	uint64_t	offset;
	uint64_t	bytes;
} r5_vram_hole;


static inline int
r5_vram_overlap(uint64_t a, uint64_t aBytes, uint64_t b, uint64_t bBytes)
{
	if (aBytes == 0 || bBytes == 0)
		return 0;
	if (a + aBytes < a || b + bBytes < b)
		return 1;
	return a < b + bBytes && b < a + aBytes;
}


static inline int
r5_vram_is_psp_claim(uint64_t offset, uint64_t bytes, uint64_t align)
{
	return bytes == R5_VRAM_PSP_TMR_BYTES
		&& align == R5_VRAM_PSP_TMR_BYTES
		&& offset == R5_VRAM_PSP_TMR_OFFSET;
}


/* 1 when *placed is inside [poolStart, poolEnd) and outside every hole,
 * except the one PSP claim above. cursor walks forward. */
static inline int
r5_vram_bump(uint64_t cursor, uint64_t poolStart, uint64_t poolEnd,
	uint64_t bytes, uint64_t align, const r5_vram_hole* holes, int holeCount,
	uint64_t* placed)
{
	int step;

	if (placed == NULL || bytes == 0 || poolEnd < poolStart)
		return 0;
	if (align < 4096 || (align & (align - 1)) != 0)
		return 0;
	if (cursor < poolStart)
		cursor = poolStart;
	for (step = 0; step < 8; step++) {
		uint64_t aligned = (cursor + align - 1) & ~(align - 1);
		int hit = -1;
		int i;

		if (aligned < cursor || aligned > poolEnd
			|| bytes > poolEnd - aligned)
			return 0;
		if (holes != NULL) {
			for (i = 0; i < holeCount; i++) {
				if (!r5_vram_overlap(aligned, bytes, holes[i].offset,
						holes[i].bytes))
					continue;
				if (r5_vram_is_psp_claim(aligned, bytes, align)
					&& holes[i].offset == R5_VRAM_PSP_TMR_OFFSET
					&& holes[i].bytes == R5_VRAM_PSP_TMR_BYTES)
					continue;
				hit = i;
				break;
			}
		}
		if (hit < 0) {
			*placed = aligned;
			return 1;
		}
		if (holes[hit].offset + holes[hit].bytes < cursor)
			return 0;
		cursor = holes[hit].offset + holes[hit].bytes;
	}
	return 0;
}


static inline void
r5_vram_discovery_hole(uint64_t vramBytes, r5_vram_hole* hole)
{
	hole->offset = 0;
	hole->bytes = 0;
	if (vramBytes <= R5_VRAM_DISCOVERY_BYTES)
		return;
	hole->offset = vramBytes - R5_VRAM_DISCOVERY_BYTES;
	hole->bytes = R5_VRAM_DISCOVERY_BYTES;
}


static inline uint16_t
r5_vram_u16(const uint8_t* p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}


static inline uint32_t
r5_vram_u32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


/* Master data table format 2 or later, index 11: vram_usagebyfirmware.
 * The v2.1 body is start_address_in_kb, then used_by_firmware_in_kb.
 * A zero start is the beginning of VRAM. Returns 1 when a range is set. */
static inline int
r5_vram_fw_hole(const uint8_t* bios, uint32_t biosBytes, r5_vram_hole* hole)
{
	uint16_t rom;
	uint16_t data;
	uint16_t table;
	uint32_t startKb;
	uint16_t fwKb;
	uint64_t bytes;
	uint64_t offset;

	hole->offset = 0;
	hole->bytes = 0;
	if (bios == NULL || biosBytes < 0x4a)
		return 0;
	rom = r5_vram_u16(bios + 0x48);
	if ((uint32_t)rom + 34 > biosBytes)
		return 0;
	data = r5_vram_u16(bios + rom + 32);
	if ((uint32_t)data + 4 + 12 * 2 > biosBytes)
		return 0;
	if (bios[data + 2] < 2)
		return 0;
	table = r5_vram_u16(bios + data + 4 + 11 * 2);
	if (table == 0 || (uint32_t)table + 12 > biosBytes)
		return 0;
	startKb = r5_vram_u32(bios + table + 4);
	fwKb = r5_vram_u16(bios + table + 8);
	if (fwKb == 0)
		return 0;
	bytes = (uint64_t)fwKb << 10;
	offset = (uint64_t)startKb << 10;
	if (offset + bytes < offset)
		return 0;
	hole->offset = offset;
	hole->bytes = bytes;
	return 1;
}

#endif
