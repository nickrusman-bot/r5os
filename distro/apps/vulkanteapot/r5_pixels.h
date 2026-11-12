/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Compare two RGBA frames. A pixel is over the limit when any channel
 * differs by more than maxChannel. shareTenths is that count in tenths
 * of a percent (10 is 1%). The compare passes when that share is at
 * most 1%.
 */

#ifndef R5_PIXELS_H
#define R5_PIXELS_H

#include <stdint.h>

int r5_pixels_compare(const uint8_t* a, const uint8_t* b, uint32_t pixels,
	int maxChannel, int* shareTenths, int* maxDelta);

#endif
