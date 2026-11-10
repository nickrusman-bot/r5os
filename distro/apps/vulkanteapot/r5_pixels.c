/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "r5_pixels.h"

#include <stddef.h>


int
r5_pixels_compare(const uint8_t* a, const uint8_t* b, uint32_t pixels,
	int maxChannel, int* percent, int* maxDelta)
{
	uint32_t bad = 0;
	int worst = 0;
	if (a == NULL || b == NULL || pixels == 0)
		return 1;
	for (uint32_t i = 0; i < pixels; i++) {
		int failed = 0;
		for (int channel = 0; channel < 4; channel++) {
			int delta = (int)a[i * 4 + channel] - (int)b[i * 4 + channel];
			if (delta < 0)
				delta = -delta;
			if (delta > worst)
				worst = delta;
			if (delta > maxChannel)
				failed = 1;
		}
		if (failed)
			bad++;
	}
	int share = (int)((bad * 100u) / pixels);
	if (bad > 0 && share == 0)
		share = 1;
	if (percent != NULL)
		*percent = share;
	if (maxDelta != NULL)
		*maxDelta = worst;
	return bad > 0 ? 1 : 0;
}
