/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AGP aperture from amdgpu_gmc_set_agp_default and
 * gfxhub_v1_0_init_system_aperture_regs. amdgpu_agp defaults to -1, so
 * amdgpu_gmc_agp_location does not run.
 *
 * Linux v6.18 and v6.12 use the same default (start 0xffffffffffff,
 * end 0) and the same Renoir high formula. v6.18 writes AGP_BASE, BOT
 * and TOP inside the !sriov || Vega10 guard; v6.12 wrote them before
 * that guard. v6.18 gmc_v9_0_vram_gtt_location selects the sysvm path
 * with amdgpu_gmc_is_pdb0_enabled; v6.12 used xgmi.connected_to_cpu.
 * Renoir takes neither path.
 */

#ifndef R5_AGP_H
#define R5_AGP_H

#include <stdint.h>


struct r5_agp_aperture {
	uint32_t bot;
	uint32_t top;
	uint32_t low;
	uint32_t high;
};


static inline struct r5_agp_aperture
r5_gfx9_agp_aperture(uint64_t fbStart, uint64_t fbEnd)
{
	const uint64_t agpStart = 0xffffffffffffull;
	const uint64_t agpEnd = 0;
	struct r5_agp_aperture out;
	uint64_t lowMc = fbStart < agpStart ? fbStart : agpStart;
	uint64_t highFb = (fbEnd >> 18) + 1ull;
	uint64_t highAgp = agpEnd >> 18;
	out.bot = (uint32_t)(agpStart >> 24);
	out.top = (uint32_t)(agpEnd >> 24);
	out.low = (uint32_t)(lowMc >> 18);
	out.high = (uint32_t)(highFb > highAgp ? highFb : highAgp);
	return out;
}


#endif
