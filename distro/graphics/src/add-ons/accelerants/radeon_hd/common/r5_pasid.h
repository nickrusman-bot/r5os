/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Linux v6.12 amdgpu_ids.c:amdgpu_pasid_alloc. The bitmap covers 16
 * bits. amdgpu_pasid_alloc itself accepts up to 31; a wider map is
 * not built here. The IH VMID LUT write in this step stores one
 * 16-bit value.
 */

#ifndef R5_PASID_H
#define R5_PASID_H

#include <stdint.h>
#include <string.h>

struct r5_pasid_ida {
	uint32_t	word[2048];
};


static inline int
r5_pasid_taken(const struct r5_pasid_ida* ida, uint32_t id)
{
	return (ida->word[id >> 5] >> (id & 31u)) & 1u;
}


static inline void
r5_pasid_set(struct r5_pasid_ida* ida, uint32_t id, int on)
{
	uint32_t bit = 1u << (id & 31u);
	if (on)
		ida->word[id >> 5] |= bit;
	else
		ida->word[id >> 5] &= ~bit;
}


/* Positive id on success. -22 when bits is 0. -28 when the range is
 * full. bits above 16 are rejected: this bitmap is 16 wide. */
static inline int
r5_pasid_alloc(struct r5_pasid_ida* ida, unsigned bits)
{
	unsigned width;
	if (ida == 0 || bits == 0)
		return -22;
	if (bits > 16u)
		return -22;
	for (width = bits; width > 0u; width--) {
		uint32_t lo = 1u << (width - 1u);
		uint32_t hi = (1u << width) - 1u;
		uint32_t id;
		for (id = lo; id <= hi; id++) {
			if (r5_pasid_taken(ida, id))
				continue;
			r5_pasid_set(ida, id, 1);
			return (int)id;
		}
	}
	return -28;
}


static inline void
r5_pasid_free(struct r5_pasid_ida* ida, uint32_t pasid)
{
	if (ida == 0 || pasid == 0 || pasid >= 65536u)
		return;
	r5_pasid_set(ida, pasid, 0);
}

#endif
