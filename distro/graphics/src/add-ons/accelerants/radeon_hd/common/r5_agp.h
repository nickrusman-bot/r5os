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

#include "r5_gc9_regs.h"


struct r5_agp_aperture {
	uint32_t bot;
	uint32_t top;
	uint32_t low;
	uint32_t high;
};


/* amdgpu_device.c:2139 amdgpu_device_init_apu_flags. CHIP_RENOIR:
 * 0x1636 and 0x164c are AMD_APU_IS_RENOIR, every other device of that
 * chip is AMD_APU_IS_GREEN_SARDINE. gfxhub_v1_0.c adds 1 to the aperture
 * high only for RAVEN2, RENOIR or GREEN_SARDINE. v6.12 uses the same
 * three flags. Raven2 is not in this table. */
static inline int
r5_gfx9_aperture_plus(uint32_t pciId)
{
	return pciId == 0x15e7u || pciId == 0x1636u || pciId == 0x1638u
		|| pciId == 0x164cu;
}


/* MC_VM_FB_LOCATION_BASE__FB_BASE_MASK is the 24-bit field. The byte
 * address is that field shifted by 24, through the last byte of the
 * framebuffer. prepare_hw and the runtime switch both use this. */
static inline uint64_t
r5_gfx9_fb_start(uint32_t location)
{
	return (uint64_t)(location & MC_VM_FB_LOCATION_BASE__FB_BASE_MASK) << 24;
}


static inline void
r5_gfx9_fb_range(uint32_t location, uint64_t bytes, uint64_t* start,
	uint64_t* end)
{
	*start = r5_gfx9_fb_start(location);
	*end = *start + bytes - 1ull;
}


static inline struct r5_agp_aperture
r5_gfx9_agp_aperture(uint64_t fbStart, uint64_t fbEnd, int plus)
{
	const uint64_t agpStart = 0xffffffffffffull;
	const uint64_t agpEnd = 0;
	struct r5_agp_aperture out;
	uint64_t lowMc = fbStart < agpStart ? fbStart : agpStart;
	uint64_t highFb = fbEnd >> 18;
	uint64_t highAgp = agpEnd >> 18;
	if (plus)
		highFb += 1ull;
	out.bot = (uint32_t)(agpStart >> 24);
	out.top = (uint32_t)(agpEnd >> 24);
	out.low = (uint32_t)(lowMc >> 18);
	out.high = (uint32_t)(highFb > highAgp ? highFb : highAgp);
	return out;
}


#endif
