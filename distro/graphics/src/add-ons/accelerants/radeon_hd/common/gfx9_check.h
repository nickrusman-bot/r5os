/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Page-table and interrupt helpers the host test can compile on its own.
 * The bit positions are from Linux v6.12 amdgpu_vm.h and amdgpu_ih.c.
 */
#ifndef GFX9_CHECK_H
#define GFX9_CHECK_H

#include <stdint.h>

enum {
	R5_GFX9_PTE_VALID = 1u << 0,
	R5_GFX9_PTE_SYSTEM = 1u << 1,
	R5_GFX9_PTE_SNOOPED = 1u << 2,
	R5_GFX9_PTE_EXECUTABLE = 1u << 4,
	R5_GFX9_PTE_READABLE = 1u << 5,
	R5_GFX9_PTE_WRITEABLE = 1u << 6
};

/* amdgpu_vm.h AMDGPU_PTE_MTYPE_VG10 shifts by 57. GC 9.3 system memory
 * uses MTYPE_NC, which is 0, so the field stays clear. */
static const uint64_t kR5Gfx9MtypeShift = 57;
static const uint64_t kR5Gfx9MtypeSystem = 0;

static inline uint64_t
r5_gfx9_gart_pte(uint64_t physical, uint64_t flags)
{
	return (physical & ~0xfffull) | flags
		| (kR5Gfx9MtypeSystem << kR5Gfx9MtypeShift);
}

/* MC_VM_AGP_BOT and MC_VM_AGP_TOP count 16 MiB units. TOP is inclusive.
 * BOT > TOP means the window is off. BOT = TOP = 0 is the first 16 MiB. */
static inline int
r5_gfx9_agp_window(uint32_t bot, uint32_t top, uint64_t* start, uint64_t* end)
{
	if (bot > top)
		return 0;
	const uint64_t unit = 16ull * 1024ull * 1024ull;
	*start = (uint64_t)bot * unit;
	*end = ((uint64_t)top + 1ull) * unit;
	return 1;
}


/* 256 MiB, aligned, directly under the framebuffer. Rejects an overlap
 * with the system aperture. agpOn is set only when AGP BOT <= TOP, and
 * then an overlap with that window is rejected too. */
static inline int
r5_gfx9_gart_range(uint64_t fbStart, uint64_t apertureStart,
	uint64_t apertureEnd, int agpOn, uint64_t agpStart, uint64_t agpEnd,
	uint64_t* start, uint64_t* end)
{
	const uint64_t size = 256ull * 1024ull * 1024ull;
	if (fbStart < size)
		return 0;
	uint64_t base = (fbStart - size) & ~(size - 1);
	uint64_t limit = base + size;
	if (limit > fbStart)
		return 0;
	if (!(limit <= apertureStart || base >= apertureEnd))
		return 0;
	if (agpOn && !(limit <= agpStart || base >= agpEnd))
		return 0;
	*start = base;
	*end = limit;
	return 1;
}

struct r5_gfx9_ih_entry {
	uint32_t	client;
	uint32_t	source;
	uint32_t	ring;
};

/* amdgpu_ih_decode_iv_helper: client is bits 7:0, source 15:8, ring 23:16. */
static inline r5_gfx9_ih_entry
r5_gfx9_ih_parse(const uint8_t raw[32])
{
	const uint32_t* dwords = (const uint32_t*)raw;
	r5_gfx9_ih_entry entry;
	entry.client = dwords[0] & 0xffu;
	entry.source = (dwords[0] >> 8) & 0xffu;
	entry.ring = (dwords[0] >> 16) & 0xffu;
	return entry;
}

#endif
