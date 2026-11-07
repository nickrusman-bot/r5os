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

/* Linux v6.12 amdgpu_vm.h: AMDGPU_PTE_PRT is bit 51 before GFX12.
 * GFX12 moves it to bit 56. GC 9.3 uses bit 51. gmc_v9_0_get_vm_pte
 * sets this bit and clears VALID. No other GMC 9 register is written
 * for a PRT mapping. */
static const uint64_t kR5Gfx9PtePrt = 1ull << 51;

/* 2 is a valid mapping, 1 is PRT, 0 is an empty entry. */
static inline int
r5_gfx9_pte_kind(uint64_t pte)
{
	if ((pte & (uint64_t)R5_GFX9_PTE_VALID) != 0)
		return 2;
	if ((pte & kR5Gfx9PtePrt) != 0)
		return 1;
	return 0;
}

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


/* Linux v6.12 gmc_v9_0.c gmc_v9_0_get_coherence_flags, the branch that
 * is not GC 9.4. A system page that is not UNCACHED and not COHERENT
 * is MTYPE_NC. That function also maps user VRAM the same way.
 * vram_live stays 0, so a user VRAM page still goes through
 * r5_gfx9_gart_pte (MTYPE_UC) until a boot compares the two.
 * Coherence sets SNOOPED on every system page, including write
 * combined. ttm_tt_pte_flags leaves SNOOPED clear for
 * ttm_write_combined, and that is the snoop bit used here. */
struct r5_gfx9_mtype_policy {
	uint32_t	gc_major;
	uint32_t	gc_minor;
	uint32_t	system_mtype;
	uint32_t	vram_mtype;
	int		vram_live;
	const char*	source;
};

static const struct r5_gfx9_mtype_policy kR5Gfx93Mtype = {
	9, 3, 0, 0, 0,
	"Linux v6.12 gmc_v9_0.c:gmc_v9_0_get_coherence_flags"
};

/* KernelExport.h: B_WRITE_COMBINING_MEMORY is (2 << 28). 0 is the
 * default write-back area type. */
static inline uint32_t
r5_gfx9_cpu_memory_type(int writeCombined)
{
	return writeCombined ? (2u << 28) : 0u;
}

static inline uint64_t
r5_gfx9_user_pte(uint64_t physical, uint64_t flags, int gtt,
	int writeCombined, const struct r5_gfx9_mtype_policy* policy)
{
	const uint64_t mask = kR5Gfx9MtypeUc << kR5Gfx9MtypeShift;
	uint64_t pte = (physical & ~0xfffull) | (flags & ~mask);
	uint32_t mtype;
	if (!gtt) {
		if (policy == 0 || policy->vram_live == 0)
			return r5_gfx9_gart_pte(physical, flags);
		mtype = policy->vram_mtype;
	} else {
		mtype = policy == 0 ? 0u : policy->system_mtype;
		pte |= (uint64_t)R5_GFX9_PTE_SYSTEM;
		if (!writeCombined)
			pte |= (uint64_t)R5_GFX9_PTE_SNOOPED;
	}
	pte &= ~mask;
	pte |= ((uint64_t)(mtype & 3u)) << kR5Gfx9MtypeShift;
	return pte;
}

static inline uint64_t
r5_gfx9_cached_gtt_pte(uint64_t physical, uint64_t flags)
{
	return r5_gfx9_user_pte(physical, flags, 1, 0, &kR5Gfx93Mtype);
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


/* Bit in VM_INVALIDATE_ENG*_ACK. Linux gmc_v9_0_flush_gpu_tlb waits on
 * ack & (1 << vmid). VMID 0 is bit 0, so the K2 poll of bit 0 stays. */
static inline uint32_t
r5_gfx9_tlb_ack_mask(uint32_t vmid)
{
	if (vmid >= 16)
		return 0;
	return 1u << vmid;
}


/* gfxhub_v1_0_setup_vmid_config writes VMID 1..15 (i = 0..14).
 * ctx_distance is mmVM_CONTEXT1_CNTL - mmVM_CONTEXT0_CNTL (1).
 * ctx_addr_distance is the page-table base stride (2). Retry stays off. */
struct r5_gfx9_vmid_ctx {
	uint32_t	cntl;
	uint32_t	start_lo;
	uint32_t	start_hi;
	uint32_t	end_lo;
	uint32_t	end_hi;
};

static inline uint32_t
r5_gfx9_vmid_cntl(void)
{
	uint32_t cntl = 1u | (3u << 1);
	cntl |= (1u << 9) | (1u << 10);
	cntl |= (1u << 11) | (1u << 12);
	cntl |= (1u << 13) | (1u << 14);
	cntl |= (1u << 15) | (1u << 16);
	cntl |= (1u << 17) | (1u << 18);
	cntl |= (1u << 19) | (1u << 20);
	cntl |= (1u << 21) | (1u << 22);
	return cntl;
}

/* A space with a parked buffer, or a submit still ahead of the fence,
 * is not freed. holds is that buffer count. */
static inline int
r5_space_free_ok(int holds, uint64_t last, uint64_t seen)
{
	if (holds > 0)
		return 0;
	if (last > seen)
		return 0;
	return 1;
}

static inline int
r5_gfx9_vmid_ctx_regs(uint32_t vmid, struct r5_gfx9_vmid_ctx* out)
{
	uint32_t i;
	if (out == NULL || vmid < 1 || vmid > 15)
		return 0;
	i = vmid - 1;
	out->cntl = 0x0881u + i;
	out->start_lo = 0x090du + i * 2u;
	out->start_hi = out->start_lo + 1u;
	out->end_lo = 0x092du + i * 2u;
	out->end_hi = out->end_lo + 1u;
	return 1;
}


typedef struct r5_gfx9_fault {
	uint32_t	more;
	uint32_t	walker;
	uint32_t	permission;
	uint32_t	mapping;
	uint32_t	cid;
	uint32_t	rw;
} r5_gfx9_fault;

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


/* The CP reports rptr as an offset in the ring. wptr keeps counting.
 * ringDwords is a power of two. The result is rptr in that same count.
 * One slot stays free, so a distance of 0 is empty and never a full lap. */
static inline uint64_t
r5_gfx9_rptr_count(uint32_t ringDwords, uint64_t wptr, uint64_t rptrOffset)
{
	uint64_t mask = (uint64_t)ringDwords - 1u;
	return wptr - ((wptr - (rptrOffset & mask)) & mask);
}


/* Free dwords, leaving one slot so the write pointer does not land on
 * the read pointer. wptr and rptr are both the continuing dword count.
 * rptr comes from r5_gfx9_rptr_count, not from the CP register. */
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

typedef struct r5_gfx9_ih_entry {
	uint32_t	client;
	uint32_t	source;
	uint32_t	ring;
} r5_gfx9_ih_entry;

/* Linux v6.12 drivers/gpu/drm/amd/include/soc15_ih_clientid.h.
 * GRBM_CP is the EOP client. UTCL2 is the GC hub. 0x1a is reserved. */
enum {
	R5_IH_CLIENT_VMC = 0x12,
	R5_IH_CLIENT_GRBM_CP = 0x14,
	R5_IH_CLIENT_RESERVED = 0x1a,
	R5_IH_CLIENT_UTCL2 = 0x1b
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


/* GC hub fault: UTCL2, source 0, vmid_src bit 31 clear.
 * Linux v6.12 gmc_v9_0.c gmc_v9_0_process_interrupt. */
static inline int
r5_gfx9_ih_gc_fault(const uint8_t raw[32])
{
	r5_gfx9_ih_entry entry = r5_gfx9_ih_parse(raw);
	uint32_t hub = ((const uint32_t*)raw)[0] >> 31;
	return entry.client == R5_IH_CLIENT_UTCL2 && entry.source == 0 && hub == 0;
}


/* The file that owns this VMID, or -1. The first match wins. */
static inline int
r5_gfx9_fault_owner(uint32_t vmid, const uint32_t* vmids, int count)
{
	int i;
	if (vmid == 0 || vmid > 15 || vmids == NULL)
		return -1;
	for (i = 0; i < count; i++) {
		if (vmids[i] == vmid)
			return i;
	}
	return -1;
}

#endif
