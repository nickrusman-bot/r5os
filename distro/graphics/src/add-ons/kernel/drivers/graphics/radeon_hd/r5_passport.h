/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fills the AMD device block from the chip row, the discovery binary,
 * and the ATOM tables. The GFX9 ring is not touched.
 */

#ifndef R5_PASSPORT_H
#define R5_PASSPORT_H

#include "r5_gpu.h"

#include <stdint.h>

struct radeon_info;


void r5_passport_apply(struct radeon_info* info, r5_gpu_amd_info* amd,
	uint32_t* cuPerSh, uint32_t* rbPerSe);

#endif
