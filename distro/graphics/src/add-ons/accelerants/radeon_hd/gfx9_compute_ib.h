/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Pure PM4 builders for the GC 9.3 compute indirect buffer. No Haiku headers.
 * The numbers are Linux v6.12 soc15d.h, gc_9_0_offset.h and vega10_enum.h.
 */
#ifndef GFX9_COMPUTE_IB_H
#define GFX9_COMPUTE_IB_H

#include <stdint.h>

static inline uint32_t
r5_packet3(uint32_t opcode, uint32_t count)
{
	return (3u << 30) | ((count & 0x3fffu) << 16) | ((opcode & 0xffu) << 8);
}

static inline uint32_t
r5_sh_offset(uint32_t index)
{
	/* (0x2000 + index) - PACKET3_SET_SH_REG_START. START is 0x2c00. */
	return index - 0x0c00u;
}

static inline uint32_t
r5_put(uint32_t* ib, uint32_t n, uint32_t value)
{
	ib[n] = value;
	return n + 1;
}

static inline uint32_t
r5_acquire_mem(uint32_t* ib, uint32_t n)
{
	/* gfx_v9_0_emit_mem_sync: PACKET3(ACQUIRE_MEM, 5), seven dwords. */
	const uint32_t coher = (1u << 29) | (1u << 27) | (1u << 23) | (1u << 22)
		| (1u << 18);
	n = r5_put(ib, n, r5_packet3(0x58u, 5));
	n = r5_put(ib, n, coher);
	n = r5_put(ib, n, 0xffffffffu);
	n = r5_put(ib, n, 0x00ffffffu);
	n = r5_put(ib, n, 0);
	n = r5_put(ib, n, 0);
	n = r5_put(ib, n, 0x0000000au);
	return n;
}

static inline uint32_t
r5_set_uconfig(uint32_t* ib, uint32_t n, uint32_t offset, uint32_t value)
{
	n = r5_put(ib, n, r5_packet3(0x79u, 1));
	n = r5_put(ib, n, offset);
	n = r5_put(ib, n, value);
	return n;
}

static inline uint32_t
r5_set_sh(uint32_t* ib, uint32_t n, uint32_t index, const uint32_t* values,
	uint32_t count)
{
	n = r5_put(ib, n, r5_packet3(0x76u, count));
	n = r5_put(ib, n, r5_sh_offset(index));
	for (uint32_t i = 0; i < count; i++)
		n = r5_put(ib, n, values[i]);
	return n;
}

static inline uint32_t
r5_release_counter(uint32_t* ib, uint32_t n, uint64_t address)
{
	const uint32_t eop = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| 0x14u | (5u << 8);
	n = r5_put(ib, n, r5_packet3(0x49u, 6));
	n = r5_put(ib, n, eop);
	n = r5_put(ib, n, 3u << 29);
	n = r5_put(ib, n, (uint32_t)address);
	n = r5_put(ib, n, (uint32_t)(address >> 32));
	n = r5_put(ib, n, 0);
	n = r5_put(ib, n, 0);
	n = r5_put(ib, n, 0);
	return n;
}

static inline uint32_t
r5_gfx9_ib_compute(uint32_t* ib, uint64_t shaderMc, uint32_t rsrc1,
	uint32_t rsrc2, uint64_t bufferMc, uint32_t groupsX,
	uint64_t timestampStart, uint64_t timestampEnd)
{
	uint32_t n = 0;
	uint32_t pair[2];
	uint32_t one;
	uint32_t three[3];
	uint64_t pgm = shaderMc >> 8;

	n = r5_acquire_mem(ib, n);
	/* GRBM_GFX_INDEX, seg1 0x2200. UCONFIG start 0xc000, so the offset is 0x200. */
	n = r5_set_uconfig(ib, n, 0x200u, 0xe0000000u);

	pair[0] = (uint32_t)pgm;
	pair[1] = (uint32_t)(pgm >> 32);
	n = r5_set_sh(ib, n, 0x0e0cu, pair, 2);
	pair[0] = rsrc1;
	pair[1] = rsrc2;
	n = r5_set_sh(ib, n, 0x0e12u, pair, 2);
	one = 0;
	n = r5_set_sh(ib, n, 0x0e15u, &one, 1);
	one = 0xffffffffu;
	n = r5_set_sh(ib, n, 0x0e16u, &one, 1);
	n = r5_set_sh(ib, n, 0x0e17u, &one, 1);
	n = r5_set_sh(ib, n, 0x0e19u, &one, 1);
	n = r5_set_sh(ib, n, 0x0e1au, &one, 1);
	one = 0;
	n = r5_set_sh(ib, n, 0x0e18u, &one, 1);
	three[0] = 0;
	three[1] = 0;
	three[2] = 0;
	n = r5_set_sh(ib, n, 0x0e04u, three, 3);
	three[0] = 64;
	three[1] = 1;
	three[2] = 1;
	n = r5_set_sh(ib, n, 0x0e07u, three, 3);
	pair[0] = (uint32_t)bufferMc;
	pair[1] = (uint32_t)(bufferMc >> 32);
	n = r5_set_sh(ib, n, 0x0e40u, pair, 2);

	n = r5_release_counter(ib, n, timestampStart);
	n = r5_put(ib, n, r5_packet3(0x15u, 3));
	n = r5_put(ib, n, groupsX);
	n = r5_put(ib, n, 1);
	n = r5_put(ib, n, 1);
	n = r5_put(ib, n, 1u);
	n = r5_put(ib, n, r5_packet3(0x46u, 0));
	n = r5_put(ib, n, 7u | (4u << 8));
	n = r5_release_counter(ib, n, timestampEnd);
	return n;
}

static inline uint32_t
r5_gfx9_ib_empty(uint32_t* ib, uint64_t shaderMc, uint32_t rsrc1, uint32_t rsrc2,
	uint64_t bufferMc, uint64_t timestampStart, uint64_t timestampEnd)
{
	return r5_gfx9_ib_compute(ib, shaderMc, rsrc1, rsrc2, bufferMc, 1,
		timestampStart, timestampEnd);
}

static inline uint32_t
r5_gfx9_submit(uint32_t* packet, uint64_t ibMc, uint32_t ibDwords,
	uint64_t sequenceMc, uint64_t sequence)
{
	const uint32_t eop = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| 0x14u | (5u << 8);
	packet[0] = r5_packet3(0x3fu, 2);
	packet[1] = (uint32_t)ibMc & ~3u;
	packet[2] = (uint32_t)((ibMc >> 32) & 0xffffu);
	packet[3] = (1u << 23) | ibDwords;
	packet[4] = r5_packet3(0x49u, 6);
	packet[5] = eop;
	packet[6] = 1u << 29;
	packet[7] = (uint32_t)sequenceMc;
	packet[8] = (uint32_t)(sequenceMc >> 32);
	packet[9] = (uint32_t)sequence;
	packet[10] = (uint32_t)(sequence >> 32);
	packet[11] = 0;
	return 12;
}

#endif
