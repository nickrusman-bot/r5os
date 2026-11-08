/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VMID 1 walk for GC 9.3. The layout is docs/GRAPHICS.md,
 * "GFX9 user address space (M4, not fixed)". translate_further is off,
 * so a PDE is the physical address of the next table with VALID set.
 */

#ifndef R5_VM_H
#define R5_VM_H

#include <stdint.h>

#include "gfx9_check.h"
#include "r5_gpu.h"
#include "r5_vm_size.h"

/* PDB2 is level 3, the page table is level 0. Nine bits each.
 * Bits 63:48 are not part of the walk. Linux amdgpu_gmc.h
 * AMDGPU_GMC_HOLE_MASK is 0x0000ffffffffffff, so a canonical high
 * address and that masked address select the same slots. */
static inline uint32_t
r5_vm_index(uint64_t va, uint32_t level)
{
	return (uint32_t)((va >> (12u + 9u * level)) & 0x1ffu);
}

/* v6.18 amdgpu_gem.c:848 amdgpu_gem_va_ioctl. The bottom 64 KiB, the
 * hole, and AMDGPU_VA_RESERVED_TOP are refused. v6.18 amdgpu_vm.h:175
 * is AMDGPU_VA_RESERVED_TRAP_SIZE (2ULL << 12); SEQ64 and CSA are
 * each (2ULL << 20) at lines 172 and 168. v6.12.60 has those same
 * three defines. v6.18.60 changes only the trap, to (1ULL << 16).
 * v6.12 compared the raw address and did not check the add for
 * overflow. v6.18 clears the hole bits first. */
static inline int
r5_vm_va_reserved(uint64_t va, uint64_t bytes)
{
	const uint64_t kTrap = 2ull << 12;
	const uint64_t kSeq64 = 2ull << 20;
	const uint64_t kCsa = 2ull << 20;
	const uint64_t top = kTrap + kSeq64 + kCsa;
	uint64_t masked = va & 0x0000ffffffffffffull;
	uint64_t vmSize = (r5_gfx9_vm_size().max_pfn << 12) - top;
	if (va < (1ull << 16))
		return 1;
	if (va >= 0x0000800000000000ull && va < 0xffff800000000000ull)
		return 1;
	if (masked > ~0ull - bytes)
		return 1;
	return masked + bytes > vmSize;
}


static inline int
r5_vm_va_fits(uint64_t va, uint64_t bytes)
{
	uint64_t end;
	int low;
	int high;
	if (bytes == 0 || (va & 0xfffull) != 0 || (bytes & 0xfffull) != 0)
		return 0;
	end = va + bytes;
	if (end < va)
		return 0;
	if (r5_vm_va_reserved(va, bytes))
		return 0;
	low = va >= R5_GPU_VA_OFFSET && end <= R5_GPU_VA_MAX;
	high = va >= R5_GPU_HIGH_VA_OFFSET && end <= R5_GPU_HIGH_VA_MAX;
	return low || high;
}


/* An indirect buffer is dword aligned and at most 0xfffff dwords.
 * The covered pages have to sit in the VA layout. va + bytes must not wrap. */
static inline int
r5_ib_span(uint64_t va, uint32_t dwords, uint64_t* first, uint64_t* last)
{
	uint64_t bytes;
	uint64_t end;
	uint64_t span;
	if (first == NULL || last == NULL || dwords == 0 || dwords > 0xfffffu
		|| (va & 3ull) != 0)
		return 0;
	if (va > ~0ull - (uint64_t)dwords * 4ull)
		return 0;
	bytes = (uint64_t)dwords * 4ull;
	end = va + bytes;
	*first = va & ~0xfffull;
	*last = (end - 1ull) & ~0xfffull;
	if (*last < *first)
		return 0;
	span = *last - *first + 4096ull;
	return r5_vm_va_fits(*first, span);
}

/* gpu_va_map adds the filled byte count to the GPU address. The sum
 * stays canonical. It is not masked before the walk. */
static inline int
r5_vm_page_va(uint64_t va, uint64_t filled, uint64_t* page)
{
	uint64_t sum;
	if ((va & 0xfffull) != 0 || (filled & 0xfffull) != 0 || page == 0)
		return 0;
	sum = va + filled;
	if (sum < va)
		return 0;
	*page = sum;
	return 1;
}

/* Physical address of a page-table page, then VALID. Same alignment rule
 * as the K2 page-table base. */
static inline uint64_t
r5_vm_pde(uint64_t physical)
{
	return (physical & ~0xfffull) | (uint64_t)R5_GFX9_PTE_VALID;
}


/* Linux v6.12 amdgpu_vm.h. AMDGPU_PTE_EXECUTABLE is bit 4.
 * AMDGPU_PDE_PTE is bit 54. amdgpu_vm_pt.c amdgpu_vm_pt_clear fills a
 * new leaf table with EXECUTABLE and an empty directory entry with
 * PDE_PTE. amdgpu_vm_pte_update_flags writes EXECUTABLE on an unmap
 * that is not VALID and not PRT. gmc_v9_0_get_vm_pde adds nothing else
 * while translate_further is off. */
static const uint64_t kR5VmPteEmpty = 0x10ull;
static const uint64_t kR5VmPdeEmpty = 1ull << 54;


static inline int
r5_vm_is_table(uint64_t entry)
{
	if ((entry & (uint64_t)R5_GFX9_PTE_VALID) == 0)
		return 0;
	if ((entry & kR5VmPdeEmpty) != 0)
		return 0;
	return 1;
}


struct r5_vm_page {
	uint64_t	physical;
	uint64_t*	cpu;
};

struct r5_vm {
	r5_vm_page*	pages;
	uint32_t	capacity;
	uint32_t	used;
	int			(*alloc)(void* cookie, uint64_t* physical, uint64_t** cpu);
	void*		cookie;
};


static inline int
r5_vm_find(const r5_vm* vm, uint64_t entry)
{
	uint64_t page = entry & ~0xfffull;
	for (uint32_t i = 0; i < vm->used; i++) {
		if (vm->pages[i].physical == page)
			return (int)i;
	}
	return -1;
}


/* leaf is a page table. A directory page is every other level. */
static inline int
r5_vm_new_page(r5_vm* vm, int leaf)
{
	if (vm->used >= vm->capacity || vm->alloc == NULL)
		return 0;
	uint64_t physical = 0;
	uint64_t* cpu = NULL;
	if (vm->alloc(vm->cookie, &physical, &cpu) == 0 || cpu == NULL)
		return 0;
	if (!r5_gfx9_pde_aligned(physical))
		return 0;
	uint64_t fill = leaf ? kR5VmPteEmpty : kR5VmPdeEmpty;
	for (int i = 0; i < 512; i++)
		cpu[i] = fill;
	vm->pages[vm->used].physical = physical;
	vm->pages[vm->used].cpu = cpu;
	vm->used++;
	return 1;
}


/* Four levels, root first. A missing table page is allocated. The leaf
 * stores the caller PTE. Returns 0 when the pool or the allocator is out. */
static inline int
r5_vm_map_page(r5_vm* vm, uint64_t va, uint64_t pte)
{
	if (vm == NULL || (va & 0xfffull) != 0)
		return 0;
	if (vm->used == 0 && !r5_vm_new_page(vm, 0))
		return 0;
	int index = 0;
	for (int level = 3; level >= 1; level--) {
		uint32_t slot = r5_vm_index(va, (uint32_t)level);
		uint64_t entry = vm->pages[index].cpu[slot];
		if (!r5_vm_is_table(entry)) {
			if (!r5_vm_new_page(vm, level == 1))
				return 0;
			vm->pages[index].cpu[slot] = r5_vm_pde(
				vm->pages[vm->used - 1].physical);
			index = (int)vm->used - 1;
		} else {
			index = r5_vm_find(vm, entry);
			if (index < 0)
				return 0;
		}
	}
	uint32_t leaf = r5_vm_index(va, 0);
	if (pte == 0) {
		uint64_t current = vm->pages[index].cpu[leaf];
		if ((current & kR5Gfx9PtePrt) == 0)
			vm->pages[index].cpu[leaf] = kR5VmPteEmpty;
	} else
		vm->pages[index].cpu[leaf] = pte;
	return 1;
}


static inline int
r5_vm_lookup(const r5_vm* vm, uint64_t va, uint64_t* pte)
{
	if (vm == NULL || vm->used == 0 || (va & 0xfffull) != 0)
		return 0;
	int index = 0;
	for (int level = 3; level >= 1; level--) {
		uint64_t entry = vm->pages[index].cpu[r5_vm_index(va, (uint32_t)level)];
		if (!r5_vm_is_table(entry))
			return 0;
		index = r5_vm_find(vm, entry);
		if (index < 0)
			return 0;
	}
	uint64_t leaf = vm->pages[index].cpu[r5_vm_index(va, 0)];
	if (pte != NULL)
		*pte = leaf;
	return (leaf & (uint64_t)R5_GFX9_PTE_VALID) != 0;
}


/* 1 when every page of the indirect buffer is still a valid leaf.
 * A wait drops the lock; the submit calls this again and refuses a 0. */
static inline int
r5_ib_still_mapped(const r5_vm* vm, uint64_t va, uint32_t dwords)
{
	uint64_t first = 0;
	uint64_t last = 0;
	uint64_t page;
	if (!r5_ib_span(va, dwords, &first, &last))
		return 0;
	for (page = first; ; page += 4096ull) {
		if (!r5_vm_lookup(vm, page, NULL))
			return 0;
		if (page == last)
			break;
	}
	return 1;
}


/* Linux amdgpu_map_static_csa. The same physical pages and the same
 * virtual address, written into this tree. */
static inline int
r5_csa_map_pages(r5_vm* vm, uint64_t va, const uint64_t* page,
	uint32_t count, uint64_t flags)
{
	uint32_t i;
	if (vm == NULL || page == NULL || count == 0 || (va & 0xfffull) != 0)
		return 0;
	for (i = 0; i < count; i++) {
		if (!r5_vm_map_page(vm, va + (uint64_t)i * 4096ull,
				page[i] | flags))
			return 0;
	}
	return 1;
}


/* A file tree stays until the client and every parked buffer in it
 * have dropped their reference. The counters return on the last drop. */
struct r5_file_count {
	int32_t		trees;
	int32_t		pages;
	int32_t		refs;
};


static inline void
r5_file_count_open(struct r5_file_count* count, int32_t pages)
{
	if (count == NULL)
		return;
	count->trees++;
	count->pages += pages;
	count->refs++;
}


static inline void
r5_file_count_hold(struct r5_file_count* count)
{
	if (count == NULL || count->refs <= 0)
		return;
	count->refs++;
}


/* 1 when this drop frees the tree. */
static inline int
r5_file_count_drop(struct r5_file_count* count, int32_t pages)
{
	if (count == NULL || count->refs <= 0)
		return 0;
	count->refs--;
	if (count->refs > 0)
		return 0;
	count->trees--;
	count->pages -= pages;
	return 1;
}

#endif
