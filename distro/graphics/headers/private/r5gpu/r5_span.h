/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Free-list for user VRAM. Kernel page tables stay on the bump allocator.
 */

#ifndef R5_SPAN_H
#define R5_SPAN_H

#include <stdint.h>

typedef struct {
	uint64_t	offset;
	uint64_t	bytes;
	int		used;
	int		live;
	/* Kernel address of offset, when the caller has one. A take of a
	 * free span writes zeros through it. Host tests leave it null. */
	uint8_t*	cpu;
} r5_span;

static inline void
r5_span_coalesce(r5_span* spans, int count)
{
	int merged = 1;
	while (merged) {
		merged = 0;
		for (int i = 0; i < count; i++) {
			if (!spans[i].live || spans[i].used)
				continue;
			for (int j = 0; j < count; j++) {
				if (i == j || !spans[j].live || spans[j].used)
					continue;
				if (spans[i].offset + spans[i].bytes
					!= spans[j].offset)
					continue;
				spans[i].bytes += spans[j].bytes;
				spans[j].live = 0;
				merged = 1;
			}
		}
	}
}

static inline int
r5_span_take(r5_span* spans, int count, uint64_t bytes, uint64_t* offset)
{
	for (int i = 0; i < count; i++) {
		if (!spans[i].live || spans[i].used || spans[i].bytes < bytes)
			continue;
		*offset = spans[i].offset;
		if (spans[i].bytes == bytes) {
			spans[i].used = 1;
			if (spans[i].cpu != NULL) {
				for (uint64_t n = 0; n < bytes; n++)
					spans[i].cpu[n] = 0;
			}
			return 1;
		}
		int slot = -1;
		for (int j = 0; j < count; j++) {
			if (!spans[j].live) {
				slot = j;
				break;
			}
		}
		if (slot < 0)
			return 0;
		spans[slot].live = 1;
		spans[slot].used = 0;
		spans[slot].offset = spans[i].offset + bytes;
		spans[slot].bytes = spans[i].bytes - bytes;
		spans[slot].cpu = spans[i].cpu != NULL
			? spans[i].cpu + bytes : NULL;
		spans[i].bytes = bytes;
		spans[i].used = 1;
		if (spans[i].cpu != NULL) {
			for (uint64_t n = 0; n < bytes; n++)
				spans[i].cpu[n] = 0;
		}
		return 1;
	}
	return 0;
}

static inline int
r5_span_add(r5_span* spans, int count, uint64_t offset, uint64_t bytes)
{
	for (int i = 0; i < count; i++) {
		if (spans[i].live)
			continue;
		spans[i].live = 1;
		spans[i].used = 1;
		spans[i].offset = offset;
		spans[i].bytes = bytes;
		spans[i].cpu = NULL;
		return 1;
	}
	return 0;
}

static inline void
r5_span_free(r5_span* spans, int count, uint64_t offset)
{
	for (int i = 0; i < count; i++) {
		if (!spans[i].live || spans[i].offset != offset)
			continue;
		spans[i].used = 0;
		r5_span_coalesce(spans, count);
		return;
	}
}

static inline int
r5_range_overlap(uint64_t start, uint64_t bytes, uint64_t other,
	uint64_t otherBytes)
{
	if (bytes == 0 || otherBytes == 0)
		return 0;
	uint64_t end = start + bytes;
	uint64_t otherEnd = other + otherBytes;
	if (end < start || otherEnd < other)
		return 1;
	return start < otherEnd && other < end;
}

#endif
