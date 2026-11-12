/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The job behind the test switch gpu_hang_test: one indirect buffer
 * that never finishes. WAIT_REG_MEM waits for a memory dword to equal
 * 1, and the dword stays 0. The packet is the one Linux v6.12
 * gfx_v9_0_ring_emit_pipeline_sync builds through gfx_v9_0_wait_reg_mem:
 * memory space, function "equal", ME engine, mask 0xffffffff, poll
 * interval 4. scripts/check-pm4.py checks it against Mesa sid.h.
 */

#ifndef R5_HANG_IB_H
#define R5_HANG_IB_H

#include <stdint.h>

enum {
	R5_HANG_IB_DWORDS = 7
};


/* va is the GPU address of the dword that stays 0. It must be dword
 * aligned (gfx_v9_0_wait_reg_mem: BUG_ON(addr0 & 0x3)). Returns the
 * dword count, or 0 when the buffer is too small or va is unaligned. */
static inline uint32_t
r5_hang_ib(uint32_t* out, uint32_t capacity, uint64_t va)
{
	if (out == 0 || capacity < (uint32_t)R5_HANG_IB_DWORDS || (va & 3u) != 0)
		return 0;
	/* PACKET3(PACKET3_WAIT_REG_MEM 0x3C, 5) */
	out[0] = 0xc0053c00u;
	/* WAIT_REG_MEM_FUNCTION(3) equal | WAIT_REG_MEM_MEM_SPACE(1) */
	out[1] = 0x00000013u;
	out[2] = (uint32_t)va;
	out[3] = (uint32_t)(va >> 32);
	/* reference */
	out[4] = 1u;
	/* mask */
	out[5] = 0xffffffffu;
	/* poll interval */
	out[6] = 4u;
	return R5_HANG_IB_DWORDS;
}

#endif
