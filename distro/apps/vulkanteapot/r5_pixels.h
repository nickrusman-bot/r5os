/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compare two RGBA frames. A pixel fails when any channel differs by
 * more than maxChannel. The percent is the share of those pixels.
 */

#ifndef R5_PIXELS_H
#define R5_PIXELS_H

#include <stdint.h>

int r5_pixels_compare(const uint8_t* a, const uint8_t* b, uint32_t pixels,
	int maxChannel, int* percent, int* maxDelta);

#endif
