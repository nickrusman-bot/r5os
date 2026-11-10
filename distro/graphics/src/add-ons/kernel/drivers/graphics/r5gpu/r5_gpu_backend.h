/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The boundary between the GPU core and a vendor backend. The core
 * calls only this struct. A null function returns B_NOT_SUPPORTED.
 * No AMD register type appears here.
 */

#ifndef R5_GPU_BACKEND_H
#define R5_GPU_BACKEND_H

#include <stddef.h>
#include <stdint.h>

typedef struct r5_gpu_backend_ops {
	void*		(*device)(void);
	int			(*read_reg)(void* info, uint32_t offset, uint32_t* value);
	int			(*vram_bytes)(void* info, uint64_t* bytes);
	int			(*vram_phys)(void* info, uint64_t byteOffset, uint64_t* phys);
	int			(*vram_alloc)(uint64_t bytes, uint64_t* offset);
	void		(*vram_free)(uint64_t offset);
	uint64_t	(*encode_pte)(uint64_t physical, uint32_t flags, int system);
	int			(*vm_map)(void* vm, uint64_t va, uint64_t pte);
	int			(*vm_lookup)(void* vm, uint64_t va);
	int			(*map_prt)(void* vm, uint64_t va);
	int			(*vm_flush)(void);
	int			(*vmid_prepare)(void);
	int			(*vmid_per_file)(void);
	void*		(*shared_vm)(void);
	int			(*file_vm_init)(void* vm, void* pages, uint32_t pagesN);
	uint32_t	(*commit_dwords)(uint32_t count, int fileTree, uint32_t pasid);
	int			(*user_room)(uint32_t dwords);
	uint64_t	(*user_seen)(void);
	void		(*set_debug)(int on);
	void		(*set_job_pasid)(uint32_t pasid, uint64_t client);
	void		(*set_file_root)(uint64_t physical);
	void		(*set_shared_root)(void);
	void		(*touch_root)(void);
	int			(*user_submit)(const uint64_t* va, const uint32_t* dwords,
				const uint32_t* vmid, const uint32_t* flags, uint32_t count,
				uint64_t fenceCtx, int32_t* preamble, uint64_t* sequence);
	int			(*get_info)(void* client, void* buffer, size_t length);
	int			(*debug_switch)(void* buffer, size_t length);
	int			(*stage_p)(void* info, void* out);
	int			(*stage_u)(void* info);
	int			(*stage_r1)(void* info, uint32_t which, uint64_t x, uint64_t y,
				uint32_t* oldWord, uint32_t* newWord);
	int			(*log_state)(const char* why, char* line, uint32_t bytes);
	int			(*dump)(const char* why);
	int			(*fault_take)(uint32_t* pasid, uint32_t* vmid, uint64_t* addr);
	int			(*map_vram)(uint64_t offset, uint64_t bytes, void** address);
} r5_gpu_backend_ops;

extern r5_gpu_backend_ops gR5GpuOps;

#endif
