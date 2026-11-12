/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * R1 packets. R1a is a WRITE_DATA of one dword. R1b is the end-of-pipe
 * RELEASE_MEM from gfx_v9_0_ring_emit_fence. R1c's indirect buffer is
 * the same WRITE_DATA, aimed at a fixed page. v6.12 and v6.18 use the
 * same RELEASE_MEM layout for this event.
 */

#ifndef R5_STAGE_R1_H
#define R5_STAGE_R1_H

#include "r5_cs.h"

#include <stdint.h>


static inline uint32_t
r5_r1_write(uint32_t* out, uint32_t cap, uint64_t va, uint32_t value)
{
	uint32_t n = 0;
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x37, 4));
	n = r5_cs_put(out, n, cap, 0x40100500u);
	n = r5_cs_put(out, n, cap, (uint32_t)va);
	n = r5_cs_put(out, n, cap, (uint32_t)(va >> 32));
	n = r5_cs_put(out, n, cap, value);
	n = r5_cs_put(out, n, cap, 0);
	if (n != 6u || (out != 0 && n > cap))
		return 0;
	return n;
}


static inline uint32_t
r5_r1_release(uint32_t* out, uint32_t cap, uint64_t va, uint32_t value)
{
	uint32_t n = r5_cs_eop_fence(out, cap, 0, va, value);
	if (n != 8u)
		return 0;
	return n;
}


#endif
