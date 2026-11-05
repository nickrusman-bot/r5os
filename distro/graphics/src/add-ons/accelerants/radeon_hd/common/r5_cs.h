/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GFX IB and RELEASE_MEM for VMID 1. The dword layout follows Linux v6.12
 * gfx_v9_0_ring_emit_ib_gfx and gfx_v9_0_ring_emit_fence. The upper address
 * is the full 32 bits: a high VA does not fit in 16 bits.
 */

#ifndef R5_CS_H
#define R5_CS_H

#include <stdint.h>

/* The kernel fence page is mapped here. The 64-bit sequence is at +2048,
 * past the K0/K2 fence words, and 8-byte aligned. */
static const uint64_t kR5UserFenceVa = 0x10000ull + 2048ull;
static const uint32_t kR5UserFenceOffset = 2048u;

static inline uint32_t
r5_cs_packet3(uint32_t opcode, uint32_t count)
{
	return (3u << 30) | ((count & 0x3fffu) << 16) | ((opcode & 0xffu) << 8);
}


/* gfx_v9_0_ring_emit_ib_gfx: length in the low bits, VMID in bits 24-27.
 * The GFX path does not set INDIRECT_BUFFER_VALID. That bit is the
 * compute path. */
static inline uint32_t
r5_cs_ib_control(uint32_t dwords, uint32_t vmid)
{
	return (dwords & 0xfffffu) | ((vmid & 0xfu) << 24);
}


static inline void
r5_cs_gfx_ib(uint32_t packet[12], uint64_t ib, uint32_t dwords,
	uint32_t vmid, uint64_t fence, uint64_t sequence)
{
	packet[0] = r5_cs_packet3(0x3f, 2);
	packet[1] = (uint32_t)ib;
	packet[2] = (uint32_t)(ib >> 32);
	packet[3] = r5_cs_ib_control(dwords, vmid);
	packet[4] = r5_cs_packet3(0x49, 6);
	/* EOP_TCL1 | EOP_TC | EOP_TC_WB | EOP_TC_MD | CACHE_FLUSH_AND_INV_TS | EVENT_INDEX 5 */
	packet[5] = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| 0x14u | (5u << 8);
	/* DATA_SEL(2) is a 64-bit sequence. INT_SEL(2) raises the EOP interrupt. */
	packet[6] = (2u << 29) | (2u << 24);
	packet[7] = (uint32_t)fence;
	packet[8] = (uint32_t)(fence >> 32);
	packet[9] = (uint32_t)sequence;
	packet[10] = (uint32_t)(sequence >> 32);
	packet[11] = 0;
}

#endif
