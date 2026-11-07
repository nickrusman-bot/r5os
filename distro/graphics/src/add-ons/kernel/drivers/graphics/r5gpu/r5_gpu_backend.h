/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The one boundary between the vendor-neutral GPU core and a vendor
 * backend. The core calls only this struct. A null function returns
 * B_NOT_SUPPORTED. No AMD type appears here.
 */

#ifndef R5_GPU_BACKEND_H
#define R5_GPU_BACKEND_H

#include <stdint.h>

/* Every function returns a status. B_OK (0) is success. A return of 1
 * is a failure, so a caller writes `!= B_OK` and never `!op()`.
 * vmid_lookup returns B_OK when that page is mapped and
 * B_ENTRY_NOT_FOUND when it is not. encode_pte writes *pte.
 * poll_retired writes *seen.
 *
 * Required ops are checked once, before the render node is published.
 * A missing required op means there is no /dev/gpu node. Optional ops
 * stay NULL and are checked at the call.
 * Required: fill_vendor_info, read_reg, vram_alloc, vram_free,
 * encode_pte, vm_map, vm_unmap, tlb_flush, vmid_prepare, vmid_lookup,
 * sh_mem, submit, poll_retired, address_open, address_close, limits,
 * note_retired, wait_room, space_lost, space_hold, space_drop.
 * Optional: set_cpu_type, vm_prt, ih_decode, fault_decode, dump,
 * stage_p, stage_u, bind, space_counts. */
typedef struct r5_gpu_backend_ops {
	int			(*fill_vendor_info)(void* info, void* block, uint32_t bytes);
	/* B_OK writes *value. B_NOT_ALLOWED is an offset this backend does
	 * not publish. Bit 31 is R5_GPU_QUERY_VRAM_BASE, not a READ_REG
	 * offset: the core rejects that bit before an ioctl reaches here. */
	int			(*read_reg)(void* info, uint32_t offset, uint32_t* value);
	int			(*vram_alloc)(uint64_t bytes, uint64_t* offset);
	int			(*vram_free)(uint64_t offset);
	int			(*set_cpu_type)(int area, uint64_t physical, uint32_t type);
	int			(*encode_pte)(uint64_t physical, uint32_t flags, int system,
				int writeCombine, uint64_t* pte);
	int			(*vm_map)(void* as, uint64_t va, uint64_t pte);
	int			(*vm_unmap)(void* as, uint64_t va, uint64_t bytes);
	int			(*vm_prt)(void* as, uint64_t va, uint64_t bytes);
	int			(*tlb_flush)(void* as);
	int			(*vmid_prepare)(void);
	int			(*vmid_lookup)(void* as, uint64_t va);
	int			(*sh_mem)(uint32_t vmid);
	int			(*submit)(void* as, const uint64_t* va, const uint32_t* dwords,
				uint32_t count, uint64_t* sequence);
	int			(*poll_retired)(uint64_t* seen);
	int			(*ih_decode)(const uint32_t* words, uint32_t count,
				uint32_t* source, uint64_t* address);
	int			(*fault_decode)(uint32_t addrLo, uint32_t addrHi,
				uint64_t* address);
	int			(*dump)(const char* why);
	int			(*stage_p)(void* info, void* out);
	int			(*stage_u)(void* info);
	/* One address space per open file. These are internal. They are
	 * not ioctls. address_open writes *as. limits writes the caps.
	 * note_retired frees a parked space whose last job has retired. */
	int			(*address_open)(void** as);
	int			(*address_close)(void* as, uint64_t seen);
	int			(*bind)(void* as);
	int			(*limits)(uint64_t* vramCap, uint64_t* gttCap);
	int			(*note_retired)(uint64_t seen);
	/* Room for a submit that may start with the VM switch. Does not
	 * reserve a VMID. The caller flushes, then submit reserves. */
	int			(*wait_room)(uint32_t count);
	/* B_OK when this address space is still running. ECANCELED when a
	 * GC hub fault marked it lost. */
	int			(*space_lost)(void* as);
	/* A parked buffer holds its address space until gpu_bo_release
	 * has unmapped it. note_retired does not free a held space. */
	int			(*space_hold)(void* as);
	int			(*space_drop)(void* as);
	/* Pages and a held VMID for this space, then the device totals.
	 * pt_pages includes the kernel page tables. */
	int			(*space_counts)(void* as, uint32_t* pt_pages,
				uint32_t* vmid, uint32_t* pt_pages_all, uint32_t* vmids_all);
} r5_gpu_backend_ops;

extern r5_gpu_backend_ops gR5GpuOps;

static inline int
r5_gpu_ops_ready(const r5_gpu_backend_ops* ops)
{
	if (ops == NULL)
		return 0;
	if (ops->fill_vendor_info == NULL || ops->read_reg == NULL
		|| ops->vram_alloc == NULL || ops->vram_free == NULL
		|| ops->encode_pte == NULL || ops->vm_map == NULL
		|| ops->vm_unmap == NULL || ops->tlb_flush == NULL
		|| ops->vmid_prepare == NULL || ops->vmid_lookup == NULL
		|| ops->sh_mem == NULL || ops->submit == NULL
		|| ops->poll_retired == NULL || ops->address_open == NULL
		|| ops->address_close == NULL || ops->limits == NULL
		|| ops->note_retired == NULL || ops->wait_room == NULL
		|| ops->space_lost == NULL || ops->space_hold == NULL
		|| ops->space_drop == NULL)
		return 0;
	return 1;
}

/* Core query, not a published READ_REG offset. The low 24 bits of the
 * framebuffer location, the same mask the mapper already applied. */
static const uint32_t R5_GPU_QUERY_VRAM_BASE = 0x80000000u;

#endif
