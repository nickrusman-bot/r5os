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

#endif
