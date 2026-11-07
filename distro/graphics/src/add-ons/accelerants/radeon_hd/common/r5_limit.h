/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Per-file GPU memory caps. VRAM is at most half of the real carveout.
 * GTT is at most a quarter of system RAM. The carveout is not the BAR.
 */

#ifndef R5_LIMIT_H
#define R5_LIMIT_H

#include <stdint.h>

static inline uint64_t
r5_limit_vram(uint64_t carveout)
{
	return carveout / 2u;
}


static inline uint64_t
r5_limit_gtt(uint64_t ram)
{
	return ram / 4u;
}


/* 0 when the addition would pass the cap. */
static inline int
r5_limit_allow(uint64_t used, uint64_t add, uint64_t cap)
{
	if (cap == 0 || add == 0 || add > cap || used > cap - add)
		return 0;
	return 1;
}

#endif
