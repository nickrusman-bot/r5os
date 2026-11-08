/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
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


#endif
