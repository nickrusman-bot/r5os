/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Dirty rows become DMA_DATA packets. A band is the full bytes-per-row
 * of a contiguous run of rows. The packet byte count is 21 bits, so a
 * longer band is several packets of whole rows.
 */

#ifndef GPU_COPY_BANDS_H
#define GPU_COPY_BANDS_H

#include <stdint.h>

struct r5_copy_rows {
	int32_t	top;
	int32_t	bottom;
};

struct r5_copy_band {
	uint32_t	y;
	uint32_t	rows;
};

static inline int
r5_copy_bands(const r5_copy_rows* rows, int count, int height,
	r5_copy_band* out, int outMax)
{
	if (out == 0 || height <= 0 || outMax < 0)
		return -1;
	if (count > 0 && rows == 0)
		return -1;
	r5_copy_band stacked[64];
	int n = 0;
	for (int i = 0; i < count; i++) {
		int top = rows[i].top;
		int bottom = rows[i].bottom;
		if (bottom < top)
			continue;
		if (top < 0)
			top = 0;
		if (bottom >= height)
			bottom = height - 1;
		if (top > bottom)
			continue;
		int placed = 0;
		for (int b = 0; b < n; b++) {
			uint32_t bandBottom = stacked[b].y + stacked[b].rows - 1u;
			if ((uint32_t)bottom + 1u < stacked[b].y
				|| (uint32_t)top > bandBottom + 1u)
				continue;
			uint32_t y = stacked[b].y;
			if ((uint32_t)top < y)
				y = (uint32_t)top;
			uint32_t end = bandBottom;
			if ((uint32_t)bottom > end)
				end = (uint32_t)bottom;
			stacked[b].y = y;
			stacked[b].rows = end - y + 1u;
			placed = 1;
			break;
		}
		if (!placed) {
			if (n >= 64)
				return -1;
			stacked[n].y = (uint32_t)top;
			stacked[n].rows = (uint32_t)(bottom - top + 1);
			n++;
		}
	}
	for (int a = 0; a < n; a++) {
		for (int b = a + 1; b < n; b++) {
			if (stacked[b].y < stacked[a].y) {
				r5_copy_band swap = stacked[a];
				stacked[a] = stacked[b];
				stacked[b] = swap;
			}
		}
	}
	int merged = 0;
	for (int i = 0; i < n; i++) {
		if (merged > 0) {
			r5_copy_band* prev = &stacked[merged - 1];
			uint32_t prevEnd = prev->y + prev->rows;
			if (stacked[i].y <= prevEnd) {
				uint32_t end = stacked[i].y + stacked[i].rows;
				if (end > prevEnd)
					prev->rows = end - prev->y;
				continue;
			}
		}
		stacked[merged++] = stacked[i];
	}
	if (merged > outMax)
		return -1;
	for (int i = 0; i < merged; i++)
		out[i] = stacked[i];
	return merged;
}


static inline int
r5_copy_bands_pm4(const r5_copy_band* bands, int count, uint32_t bytesPerRow,
	uint64_t srcVa, uint64_t dstVa, uint32_t* words, int wordMax)
{
	if (bands == 0 || words == 0 || bytesPerRow == 0 || (bytesPerRow & 3u) != 0)
		return -1;
	const uint32_t kMaxBytes = 0x1fffffu;
	uint32_t rowsPerPacket = kMaxBytes / bytesPerRow;
	if (rowsPerPacket == 0)
		return -1;
	int n = 0;
	for (int i = 0; i < count; i++) {
		uint32_t left = bands[i].rows;
		uint32_t row = bands[i].y;
		while (left > 0) {
			uint32_t take = left;
			if (take > rowsPerPacket)
				take = rowsPerPacket;
			if (n + 7 > wordMax)
				return -1;
			uint64_t src = srcVa + (uint64_t)row * bytesPerRow;
			uint64_t dst = dstVa + (uint64_t)row * bytesPerRow;
			uint32_t bytes = take * bytesPerRow;
			words[n++] = 0xc0055000u;
			words[n++] = 0x60200000u;
			words[n++] = (uint32_t)src;
			words[n++] = (uint32_t)(src >> 32);
			words[n++] = (uint32_t)dst;
			words[n++] = (uint32_t)(dst >> 32);
			words[n++] = 0x80000000u | bytes;
			row += take;
			left -= take;
		}
	}
	return n;
}

#endif
