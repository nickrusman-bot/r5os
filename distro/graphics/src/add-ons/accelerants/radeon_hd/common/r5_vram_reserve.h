/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Ranges the VRAM pool must not hand out. Offsets are bytes from the
 * start of the visible window. A range that starts past that window
 * is not reserved here.
 */

#ifndef R5_VRAM_RESERVE_H
#define R5_VRAM_RESERVE_H

#include <stdint.h>

/* Linux amdgpu_ttm_reserve_tmr. The PSP TMR on this pool is the 4 MiB
 * block at 0xc00000. The discovery TMR is 10 KiB, ending 64 KiB below
 * the end of VRAM. */
#define R5_PSP_TMR_OFFSET 0xc00000ull
#define R5_PSP_TMR_BYTES (4ull * 1024ull * 1024ull)
#define R5_DISCOVERY_TMR_TAIL (64ull * 1024ull)
#define R5_DISCOVERY_TMR_BYTES (10ull * 1024ull)

typedef struct {
	uint64_t	offset;
	uint64_t	bytes;
} r5_vram_range;


static inline int
r5_vram_overlaps(uint64_t offset, uint64_t bytes, uint64_t hole,
	uint64_t holeBytes)
{
	if (bytes == 0 || holeBytes == 0)
		return 0;
	if (offset >= hole + holeBytes)
		return 0;
	if (hole >= offset + bytes)
		return 0;
	return 1;
}


static inline int
r5_vram_push(r5_vram_range* out, int count, int cap, uint64_t offset,
	uint64_t bytes, uint64_t windowBytes)
{
	if (out == 0 || count >= cap || bytes == 0 || windowBytes == 0)
		return count;
	if (offset >= windowBytes)
		return count;
	if (offset + bytes < offset || offset + bytes > windowBytes)
		bytes = windowBytes - offset;
	out[count].offset = offset;
	out[count].bytes = bytes;
	return count + 1;
}


/* fwOffset and fwBytes are fw_vram_usage. Zero bytes means that
 * reservation is not known. */
static inline int
r5_vram_reserve(uint64_t vramBytes, uint64_t windowBytes, uint64_t fwOffset,
	uint64_t fwBytes, r5_vram_range* out, int cap)
{
	int count = 0;
	count = r5_vram_push(out, count, cap, R5_PSP_TMR_OFFSET,
		R5_PSP_TMR_BYTES, windowBytes);
	if (vramBytes > R5_DISCOVERY_TMR_TAIL) {
		count = r5_vram_push(out, count, cap,
			vramBytes - R5_DISCOVERY_TMR_TAIL, R5_DISCOVERY_TMR_BYTES,
			windowBytes);
	}
	count = r5_vram_push(out, count, cap, fwOffset, fwBytes, windowBytes);
	return count;
}


/* The next offset at or after cursor, aligned, that misses every hole.
 * Returns 1 and writes the offset, or 0 when the pool cannot hold it. */
static inline int
r5_vram_place(uint64_t start, uint64_t end, uint64_t cursor, uint64_t bytes,
	uint64_t align, const r5_vram_range* holes, int holeCount,
	uint64_t* _offset)
{
	int guard;
	if (_offset == 0 || bytes == 0 || align == 0
		|| (align & (align - 1ull)) != 0)
		return 0;
	if (cursor < start)
		cursor = start;
	for (guard = 0; guard < 8; guard++) {
		uint64_t aligned = (cursor + align - 1ull) & ~(align - 1ull);
		int index;
		if (aligned < cursor || aligned > end || bytes > end - aligned)
			return 0;
		for (index = 0; index < holeCount; index++) {
			uint64_t next;
			if (holes == 0)
				break;
			if (!r5_vram_overlaps(aligned, bytes, holes[index].offset,
					holes[index].bytes))
				continue;
			next = holes[index].offset + holes[index].bytes;
			if (next <= cursor)
				continue;
			cursor = next;
			aligned = ~0ull;
			break;
		}
		if (aligned != ~0ull) {
			*_offset = aligned;
			return 1;
		}
	}
	return 0;
}

#endif
