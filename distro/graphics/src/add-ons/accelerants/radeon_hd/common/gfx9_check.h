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

/* amdgpu_vm.h AMDGPU_PTE_MTYPE_VG10 shifts by 57 and keeps two bits.
 * vega10_enum.h MTYPE_UC is 3. gmc_v9_0_gart_init stores
 * AMDGPU_PTE_MTYPE_VG10(0, MTYPE_UC) | AMDGPU_PTE_EXECUTABLE. */
static const uint64_t kR5Gfx9MtypeShift = 57;
static const uint64_t kR5Gfx9MtypeUc = 3;

/* gmc_v9_0_get_vm_pde BUG_ON: 64-byte alignment, top 16 bits clear. */
static const uint64_t kR5Gfx9PdeAlign = 0xFFFF00000000003Full;

static inline uint64_t
r5_gfx9_gart_pte(uint64_t physical, uint64_t flags)
{
	const uint64_t mask = kR5Gfx9MtypeUc << kR5Gfx9MtypeShift;
	return (physical & ~0xfffull) | (flags & ~mask) | mask;
}


/* (MC_VM_FB_OFFSET << 24) + table byte offset, then AMDGPU_PTE_VALID.
 * The caller rejects an address that fails kR5Gfx9PdeAlign. */
static inline uint64_t
r5_gfx9_page_table_base(uint32_t fbOffset, uint64_t tableOffset)
{
	uint64_t physical = ((uint64_t)(fbOffset & 0x00ffffffu) << 24)
		+ tableOffset;
	return physical | (uint64_t)R5_GFX9_PTE_VALID;
}


static inline int
r5_gfx9_pde_aligned(uint64_t address)
{
	return (address & kR5Gfx9PdeAlign) == 0;
}


/* gc_9_0_sh_mask.h VM_INVALIDATE_ENG0_REQ. FLUSH_TYPE is two bits at 16.
 * L2 PTEs, PDE0, PDE1, PDE2 and L1 PTEs follow, the same bits
 * gmc_v9_0_get_invalidate_req sets. CLEAR_PROTECTION_FAULT_STATUS_ADDR stays off. */
enum {
	R5_GFX9_INV_VMID_MASK = 0x0000ffffu,
	R5_GFX9_INV_FLUSH_SHIFT = 16,
	R5_GFX9_INV_FLUSH_MASK = 0x3u,
	R5_GFX9_INV_L2_PTES = 1u << 18,
	R5_GFX9_INV_L2_PDE0 = 1u << 19,
	R5_GFX9_INV_L2_PDE1 = 1u << 20,
	R5_GFX9_INV_L2_PDE2 = 1u << 21,
	R5_GFX9_INV_L1_PTES = 1u << 22
};

static inline uint32_t
r5_gfx9_tlb_req(uint32_t vmid, uint32_t flushType)
{
	return ((1u << vmid) & R5_GFX9_INV_VMID_MASK)
		| ((flushType & R5_GFX9_INV_FLUSH_MASK) << R5_GFX9_INV_FLUSH_SHIFT)
		| R5_GFX9_INV_L2_PTES | R5_GFX9_INV_L2_PDE0
		| R5_GFX9_INV_L2_PDE1 | R5_GFX9_INV_L2_PDE2
		| R5_GFX9_INV_L1_PTES;
}


struct r5_gfx9_fault {
	uint32_t	more;
	uint32_t	walker;
	uint32_t	permission;
	uint32_t	mapping;
	uint32_t	cid;
	uint32_t	rw;
};

/* gc_9_0_sh_mask.h VM_L2_PROTECTION_FAULT_STATUS. */
static inline r5_gfx9_fault
r5_gfx9_fault_decode(uint32_t status)
{
	r5_gfx9_fault fault;
	fault.more = status & 1u;
	fault.walker = (status >> 1) & 7u;
	fault.permission = (status >> 4) & 0xfu;
	fault.mapping = (status >> 8) & 1u;
	fault.cid = (status >> 9) & 0x1ffu;
	fault.rw = (status >> 18) & 1u;
	return fault;
}


/* FAULT_ADDR is a page number. The byte address is that number << 12. */
static inline uint64_t
r5_gfx9_fault_address(uint32_t lo, uint32_t hi)
{
	return (((uint64_t)(hi & 0xfu) << 32) | (uint64_t)lo) << 12;
}


/* amdgpu_ring_write with support_64bit_ptrs. wptr keeps counting.
 * The ring slot is that count masked to the dword buffer. dwords is
 * a power of two. */
static inline uint32_t
r5_gfx9_ring_index(uint64_t wptr, uint32_t i, uint32_t dwords)
{
	return (uint32_t)((wptr + i) & (uint64_t)(dwords - 1u));
}


/* Free dwords, leaving one slot so the write pointer does not land on
 * the read pointer. rptr and wptr are both the continuing dword count. */
static inline uint64_t
r5_gfx9_ring_space(uint32_t ringDwords, uint64_t wptr, uint64_t rptr)
{
	if (wptr < rptr)
		return 0;
	uint64_t used = wptr - rptr;
	if (used + 1u >= ringDwords)
		return 0;
	return (uint64_t)ringDwords - used - 1u;
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
