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

/* PDB2 is level 3, the page table is level 0. Nine bits each. */
static inline uint32_t
r5_vm_index(uint64_t va, uint32_t level)
{
	return (uint32_t)((va >> (12u + 9u * level)) & 0x1ffu);
}

/* Physical address of a page-table page, then VALID. Same alignment rule
 * as the K2 page-table base. */
static inline uint64_t
r5_vm_pde(uint64_t physical)
{
	return (physical & ~0xfffull) | (uint64_t)R5_GFX9_PTE_VALID;
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


static inline int
r5_vm_new_page(r5_vm* vm)
{
	if (vm->used >= vm->capacity || vm->alloc == NULL)
		return 0;
	uint64_t physical = 0;
	uint64_t* cpu = NULL;
	if (vm->alloc(vm->cookie, &physical, &cpu) == 0 || cpu == NULL)
		return 0;
	if (!r5_gfx9_pde_aligned(physical))
		return 0;
	for (int i = 0; i < 512; i++)
		cpu[i] = 0;
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
	if (vm->used == 0 && !r5_vm_new_page(vm))
		return 0;
	int index = 0;
	for (int level = 3; level >= 1; level--) {
		uint32_t slot = r5_vm_index(va, (uint32_t)level);
		uint64_t entry = vm->pages[index].cpu[slot];
		if (entry == 0) {
			if (!r5_vm_new_page(vm))
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
	vm->pages[index].cpu[r5_vm_index(va, 0)] = pte;
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
		if (entry == 0)
			return 0;
		index = r5_vm_find(vm, entry);
		if (index < 0)
			return 0;
	}
	uint64_t leaf = vm->pages[index].cpu[r5_vm_index(va, 0)];
	if (pte != NULL)
		*pte = leaf;
	return leaf != 0;
}

#endif
