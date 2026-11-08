/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VM size the way amdgpu_vm_adjust_size computes it, then the context
 * register gfxhub_v1_0_setup_vmid_config writes.
 */

#ifndef R5_VM_SIZE_H
#define R5_VM_SIZE_H

#include "r5_gc9_regs.h"

#include <stdint.h>


struct r5_vm_size {
	uint32_t	vm_gb;
	uint32_t	num_level;
	uint32_t	block_size;
	uint64_t	max_pfn;
};


/* Linux v6.12 amdgpu_vm.c:amdgpu_vm_adjust_size, with the module
 * parameters left at their defaults (vm size and block size automatic).
 * min_vm_gb, max_level and max_bits are the gmc_v9_0.c arguments for
 * IP_VERSION(9, 3, 0): 256 * 1024, 3 and 48. */
static inline uint32_t
r5_fls64(uint64_t value)
{
	uint32_t bit = 0;
	if (value == 0)
		return 0;
	while (value > 1) {
		value >>= 1;
		bit++;
	}
	return bit + 1u;
}


static inline uint64_t
r5_roundup_pow2(uint64_t value)
{
	uint64_t up;
	if (value <= 1)
		return 1;
	up = 1ull << (r5_fls64(value - 1u));
	return up;
}


static inline struct r5_vm_size
r5_vm_adjust(uint32_t min_vm_gb, uint32_t max_level, uint32_t max_bits,
	uint32_t phys_ram_gb)
{
	struct r5_vm_size out;
	uint32_t max_gb = 1u << (max_bits - 30u);
	uint32_t vm_gb;
	uint64_t tmp;
	uint32_t levels;
	uint32_t guess = phys_ram_gb * 3u;
	if (guess < min_vm_gb)
		guess = min_vm_gb;
	if (guess > max_gb)
		guess = max_gb;
	vm_gb = (uint32_t)r5_roundup_pow2(guess);
	if (vm_gb > max_gb)
		vm_gb = max_gb;
	out.vm_gb = vm_gb;
	out.max_pfn = (uint64_t)vm_gb << 18;
	tmp = r5_roundup_pow2(out.max_pfn);
	levels = ((r5_fls64(tmp) - 1u) + 8u) / 9u - 1u;
	if (levels > max_level)
		levels = max_level;
	out.num_level = levels;
	out.block_size = levels > 1u ? 9u : 0u;
	return out;
}


/* gmc_v9_0.c for IP 9.3.0: amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48).
 * The fragment-size argument is not stored here. phys_ram_gb 0 keeps the
 * minimum, which is already the 48-bit maximum. */
static inline struct r5_vm_size
r5_gfx9_vm_size(void)
{
	return r5_vm_adjust(256u * 1024u, 3u, 48u, 0);
}


/* gfxhub_v1_0_setup_vmid_config. translate_further is false for GC 9.3,
 * so the block size loses 9 and the level stays. The retry bit stays
 * off: R5 has no retry-fault handler. */
static inline uint32_t
r5_gfx9_vmid_cntl(const struct r5_vm_size* size, int translate_further)
{
	uint32_t depth = size->num_level;
	uint32_t block = size->block_size;
	uint32_t depthBits = VM_CONTEXT0_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT
		- VM_CONTEXT0_CNTL__PAGE_TABLE_DEPTH__SHIFT;
	uint32_t blockBits = VM_CONTEXT0_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT
		- VM_CONTEXT0_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT;
	uint32_t cntl;
	if (translate_further)
		depth -= 1u;
	else
		block -= 9u;
	cntl = 1u << VM_CONTEXT0_CNTL__ENABLE_CONTEXT__SHIFT;
	cntl |= (depth & ((1u << depthBits) - 1u))
		<< VM_CONTEXT0_CNTL__PAGE_TABLE_DEPTH__SHIFT;
	cntl |= (block & ((1u << blockBits) - 1u))
		<< VM_CONTEXT0_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT;
	cntl |= 1u << VM_CONTEXT0_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT;
	cntl |= 1u << VM_CONTEXT0_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT;
	cntl |= 1u << VM_CONTEXT0_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT;
	cntl |= 1u << VM_CONTEXT0_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT;
	cntl |= 1u << VM_CONTEXT0_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT;
	cntl |= 1u << VM_CONTEXT0_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT;
	cntl |= 1u << VM_CONTEXT0_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT;
	return cntl;
}


static inline uint32_t
r5_gfx9_sh_mem_config(void)
{
	/* Linux v6.12 gfx_v9_0.c:gfx_v9_0_constants_init.
	 * ALIGNMENT_MODE unaligned and RETRY_DISABLE. */
	return (3u << 3) | (1u << 12);
}


static inline uint32_t
r5_gfx9_sh_mem_bases(void)
{
	/* private_aperture_start >> 48 and shared_aperture_start >> 48. */
	return (0x2000u << 16) | 0x1000u;
}


#endif
