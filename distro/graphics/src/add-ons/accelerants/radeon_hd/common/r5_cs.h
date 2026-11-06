/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GFX user job, matching the non-MCBP frame Linux puts around the
 * indirect buffers (gfx_v9_0.c emit_cntxcntl, emit_frame_cntl,
 * emit_ib_gfx, emit_fence, emit_sb, and amdgpu_ring_commit).
 * IB_BASE_HI is the full upper 32 bits. The RELEASE_MEM addresses are
 * VMID 0 MC addresses. The fence page is not mapped in VMID 1.
 */

#ifndef R5_CS_H
#define R5_CS_H

#include <stdint.h>

/* 64-bit sequence on the kernel fence page, past the K0/K2 words.
 * The 32-bit interrupt fence sits 16 bytes later and does not overlap it. */
static const uint32_t kR5UserFenceOffset = 2048u;
static const uint32_t kR5UserIrqOffset = 2064u;

/* CACHE_FLUSH_AND_INV_TS_EVENT, EVENT_INDEX 5, the EOP action bits
 * from the 6 Nov 2026 Linux capture (0x00238514). */
static const uint32_t kR5CsEvent = 0x00238514u;
/* DATA_SEL 2, no interrupt. The user sequence is 64 bits. */
static const uint32_t kR5CsRel64 = 0x40000000u;
/* DATA_SEL 1 and INT_SEL 2. The second fence raises EOP. */
static const uint32_t kR5CsRel32Int = 0x22000000u;
/* gfx_v9_0 ring nop: PACKET3(PACKET3_NOP, 0x3FFF). */
static const uint32_t kR5CsNop = 0xffff1000u;


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


/* One IB plus the fixed frame is 29 dwords. Each further IB adds 4.
 * Eight IBs is the submit limit. */
static inline uint32_t
r5_cs_user_words(uint32_t count)
{
	if (count == 0 || count > 8)
		return 0;
	return 29u + (count - 1u) * 4u;
}


/* amdgpu_ring_commit with align_mask 0xff. Zero when wptr is already
 * a multiple of 256. */
static inline uint32_t
r5_cs_pad_dwords(uint64_t wptrAfter)
{
	uint32_t mis = (uint32_t)(wptrAfter & 0xffu);
	if (mis == 0)
		return 0;
	return 256u - mis;
}


static inline uint32_t
r5_cs_user_commit(uint64_t wptr, uint32_t count)
{
	uint32_t body = r5_cs_user_words(count);
	if (body == 0)
		return 0;
	return body + r5_cs_pad_dwords(wptr + body);
}


static inline uint32_t
r5_cs_put(uint32_t* out, uint32_t n, uint32_t cap, uint32_t word)
{
	if (out != 0 && n < cap)
		out[n] = word;
	return n + 1u;
}


/* Unpadded frame. out may be NULL to count. Returns 0 when cap is short
 * or an IB is not dword aligned. */
static inline uint32_t
r5_cs_user_body(uint32_t* out, uint32_t cap, const uint64_t* ib,
	const uint32_t* dwords, const uint32_t* vmid, uint32_t count,
	uint64_t userFence, uint64_t sequence, uint64_t irqFence)
{
	uint32_t n = 0;
	uint32_t i;
	if (r5_cs_user_words(count) == 0 || ib == 0 || dwords == 0 || vmid == 0)
		return 0;
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x28, 1));
	n = r5_cs_put(out, n, cap, 0x80000000u);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x90, 0));
	n = r5_cs_put(out, n, cap, 0);
	for (i = 0; i < count; i++) {
		if ((ib[i] & 3ull) != 0 || dwords[i] == 0 || dwords[i] > 0xfffffu
			|| vmid[i] > 15u)
			return 0;
		n = r5_cs_put(out, n, cap, r5_cs_packet3(0x3f, 2));
		n = r5_cs_put(out, n, cap, (uint32_t)ib[i]);
		n = r5_cs_put(out, n, cap, (uint32_t)(ib[i] >> 32));
		n = r5_cs_put(out, n, cap, r5_cs_ib_control(dwords[i], vmid[i]));
	}
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x90, 0));
	n = r5_cs_put(out, n, cap, 0x10000000u);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x49, 6));
	n = r5_cs_put(out, n, cap, kR5CsEvent);
	n = r5_cs_put(out, n, cap, kR5CsRel64);
	n = r5_cs_put(out, n, cap, (uint32_t)userFence);
	n = r5_cs_put(out, n, cap, (uint32_t)(userFence >> 32));
	n = r5_cs_put(out, n, cap, (uint32_t)sequence);
	n = r5_cs_put(out, n, cap, (uint32_t)(sequence >> 32));
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x49, 6));
	n = r5_cs_put(out, n, cap, kR5CsEvent);
	n = r5_cs_put(out, n, cap, kR5CsRel32Int);
	n = r5_cs_put(out, n, cap, (uint32_t)irqFence);
	n = r5_cs_put(out, n, cap, (uint32_t)(irqFence >> 32));
	n = r5_cs_put(out, n, cap, (uint32_t)sequence);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x8b, 0));
	n = r5_cs_put(out, n, cap, 0);
	if (n != r5_cs_user_words(count) || (out != 0 && n > cap))
		return 0;
	return n;
}


/* gfx_v9_ring_insert_nop. One dword is the single nop 0xFFFF1000.
 * A longer pad is PACKET3(NOP, min(count - 2, 0x3FFE)) and then
 * count - 1 times that nop. count - 1 in the header reads one dword
 * past the pad. */
static inline void
r5_cs_pad(uint32_t* out, uint32_t count)
{
	uint32_t i;
	uint32_t n;
	if (out == 0 || count == 0)
		return;
	if (count == 1) {
		out[0] = kR5CsNop;
		return;
	}
	n = count - 2u;
	if (n > 0x3ffeu)
		n = 0x3ffeu;
	out[0] = r5_cs_packet3(0x10, n);
	for (i = 1; i < count; i++)
		out[i] = kR5CsNop;
}

#endif
