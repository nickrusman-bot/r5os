/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M10: which path is used (docs/GPU-ARCHITECTURE.md, "Which path is
 * used"). Pure functions, so the host test covers every case.
 */

#ifndef R5_GPU_CHECKS_H
#define R5_GPU_CHECKS_H

#include <stdint.h>

/* AGENTS.md support levels. Only a row of the radeon_hd device table
 * marked R5_GPU_LEVEL_RELEASE gets the GPU by default. 0 is a row that
 * was never given a level: every switch stays opt-in. */
#define R5_GPU_LEVEL_NONE 0u
#define R5_GPU_LEVEL_BUILDABLE 1u
#define R5_GPU_LEVEL_BOOTABLE 2u
#define R5_GPU_LEVEL_FUNCTIONAL 3u
#define R5_GPU_LEVEL_DESKTOP 4u
#define R5_GPU_LEVEL_RELEASE 5u

/* The default of one GPU switch (gfx9_ring, gfx9_kernel, gpu_render, ...)
 * when the settings do not name it. "gpu false" (the opt-out) turns every
 * switch off, whatever the settings say about that switch. */
static inline int
r5_gpu_switch_default(uint32_t level, int optOut)
{
	return !optOut && level == R5_GPU_LEVEL_RELEASE;
}

/* One switch: the settings value when it is named (0 or 1), otherwise
 * the default. named is -1 when the settings do not name the switch. */
static inline int
r5_gpu_switch(uint32_t level, int optOut, int named)
{
	if (optOut)
		return 0;
	if (named >= 0)
		return named != 0;
	return r5_gpu_switch_default(level, optOut);
}

/* The four checks, computed once per boot in the kernel. NULL means
 * "radeon_hd: gpu available"; anything else is the reason after
 * "radeon_hd: gpu not available: ". The opt-out comes first: with
 * "gpu false" nothing is started, so the other checks do not run.
 * switchedOff is a release-supported GPU whose ring or kernel stage the
 * settings turned off. selftestOk is the fixed GFX9 start: the K0 fence
 * with its readback, K1 when the interrupt is on, K2 when the GART is
 * on, each within its existing limit. */
static inline const char*
r5_gpu_check_reason(uint32_t level, int optOut, int switchedOff,
	int firmwareOk, int selftestOk)
{
	if (optOut)
		return "opt-out";
	if (level != R5_GPU_LEVEL_RELEASE)
		return "not release-supported";
	if (switchedOff)
		return "switched off";
	if (!firmwareOk)
		return "firmware";
	if (!selftestOk)
		return "selftest";
	return 0;
}

#endif
