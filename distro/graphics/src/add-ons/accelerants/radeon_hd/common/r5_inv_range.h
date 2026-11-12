/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Address of each GFXHUB invalidation engine. The step and the two
 * written values are gfxhub_v1_0_program_invalidation.
 */

#ifndef R5_INV_RANGE_H
#define R5_INV_RANGE_H

#include "r5_gc9_regs.h"

#include <stdint.h>


/* Linux v6.12 gfxhub_v1_0.c:gfxhub_v1_0_program_invalidation.
 * The loop is for (i = 0; i < 18; ++i). */
static inline uint32_t
r5_gfx9_inv_engines(void)
{
	return 18u;
}


static inline uint32_t
r5_gfx9_inv_range_lo(uint32_t engine)
{
	return mmVM_INVALIDATE_ENG0_ADDR_RANGE_LO32
		+ engine * R5_GC9_ENG_ADDR_DISTANCE;
}


static inline uint32_t
r5_gfx9_inv_range_hi(uint32_t engine)
{
	return mmVM_INVALIDATE_ENG0_ADDR_RANGE_HI32
		+ engine * R5_GC9_ENG_ADDR_DISTANCE;
}


/* The two stores in that function, not a computed mask. */
static inline uint32_t
r5_gfx9_inv_range_lo_value(void)
{
	return 0xffffffffu;
}


static inline uint32_t
r5_gfx9_inv_range_hi_value(void)
{
	return 0x1fu;
}


/* No settings line leaves the range on. A line that says false turns
 * it off. An empty line uses the same default as a missing line. */
static inline int
r5_gfx9_inv_range_chosen(int line_present, int line_on)
{
	if (!line_present)
		return 1;
	return line_on ? 1 : 0;
}


/* One store per engine, the same pair gfxhub_v1_0_program_invalidation
 * writes. Off writes nothing and returns 0. */
static inline uint32_t
r5_gfx9_inv_range_write(uint32_t* lo, uint32_t* hi, int on)
{
	uint32_t engine;
	if (!on || lo == NULL || hi == NULL)
		return 0;
	for (engine = 0; engine < r5_gfx9_inv_engines(); engine++) {
		lo[engine] = r5_gfx9_inv_range_lo_value();
		hi[engine] = r5_gfx9_inv_range_hi_value();
	}
	return r5_gfx9_inv_engines();
}


#endif
