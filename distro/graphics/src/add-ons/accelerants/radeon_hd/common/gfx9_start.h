/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The user chose this license for the shared GFX9 start on 5 Nov 2026.
 * The accelerant and the kernel driver both include this header.
 */

#ifndef GFX9_START_H
#define GFX9_START_H

#include "gfx9_check.h"
#include "r5_cs.h"

#include <SupportDefs.h>


// The allocator keeps the width of radeon_vram_alloc. The pool cursor is
// a byte offset in a window that can be larger than 4 GiB.
struct r5_gfx9_host {
	uint32		(*read32)(void* cookie, uint32 offset);
	void		(*write32)(void* cookie, uint32 offset, uint32 value);
	bool		(*mmio_fits)(void* cookie, uint32 offset);
	void		(*sleep_us)(void* cookie, uint32 micros);
	void		(*log)(void* cookie, const char* format, ...);
	status_t	(*vram_alloc)(void* cookie, uint64 bytes, uint64 align,
					uint64* offset, uint8** address);
	uint8*		(*read_firmware)(void* cookie, const char* name, size_t* bytes);
	void		(*free_firmware)(void* cookie, uint8* blob);
	uint8*		doorbell;
	uint64		doorbellBytes;
	void*		cookie;
	/* 1 around the SETUP_TMR allocation, 0 after it. NULL skips the note. */
	int			(*vram_claim)(void* cookie, int on);
};

struct r5_gfx9_results {
	char		text[8192];
	uint32		used;
};

enum {
	R5_GFX9_OWNER_NONE = 0,
	R5_GFX9_OWNER_KERNEL = 1
};

/* The K2 DMA packet and the K2 readback use this one value. */
static const uint32 kK2Pattern = 0xA5A5A5A5;

void r5_gfx9_prepare(r5_gfx9_host* host, uint64 vramBytes);
bool r5_gfx9_start(r5_gfx9_host* host, uint32 pciId, uint64 vramBytes,
	bool workload, bool compute, r5_gfx9_results* results);
void r5_gfx9_note(const char* line);
bool r5_gfx9_k1_submit(uint32* _sequence);
bool r5_gfx9_k2_dma(uint64 destMc, uint32* _sequence);
bool r5_gfx9_k2_gart_ib(uint64 ibMc, uint8* ibCpu, uint32* _sequence);
uint32 r5_gfx9_last_word();
uint64 r5_gfx9_last_sequence();
struct r5_gfx9_user_ib {
	uint64		va;
	uint32		dwords;
	uint32		vmid;
	/* AMDGPU_IB_FLAG_*. The ioctl rejects anything but 0. */
	uint32		flags;
};

bool r5_gfx9_ring_ready();
bool r5_gfx9_user_room(uint32 dwords);
/* Dwords this user job will append, including the 256-dword pad. */
uint32 r5_gfx9_user_commit_dwords(uint32 count);
/* Continuing write count, and the CP read pointer as a ring offset. */
void r5_gfx9_ring_ptrs(uint64* wptr, uint64* rptrOffset);
bool r5_gfx9_fence_offset(uint64* offset);
uint64 r5_gfx9_user_fence_mc();
/* fenceCtx is the value handed out at AMDGPU_CTX create. preambleSeen
 * is that context's preamble_presented flag. The kernel jobs use 0.
 * gpu_submit_log is the radeon_hd setting for the per-submit line.
 * gpu_debug does not print every job. */
void r5_gfx9_set_debug(int on);
bool r5_gfx9_user_submit(const r5_gfx9_user_ib* ibs, uint32 count,
	uint64 fenceCtx, int32* preambleSeen, uint64* _sequence);
uint64 r5_gfx9_user_seen();
uint64 r5_gfx9_k2_word_mc();
bool r5_gfx9_stage_ib_store(uint8* cpu, uint64 destVa);
bool r5_gfx9_stage_ib(uint8* cpu, uint64 destVa);
bool r5_gfx9_stage_wb(uint8* cpu, uint64 destVa);
void r5_gfx9_cp_dump(const char* why, uint64 pte);
bool r5_gfx9_vmid1_setup();
/* gfx9_vmid_per_file. Off unless the radeon_hd setting says so.
 * Map, unmap and lookup take the tree. There is no current tree. */
struct r5_vm;
struct r5_vm_page;
bool r5_gfx9_vmid_per_file(void);
/* The shared VMID 1 tree. A file tree is the r5_vm passed to map. */
struct r5_vm* r5_gfx9_shared_vm(void);
bool r5_gfx9_vm_map(struct r5_vm* vm, uint64 va, uint64 pte);
bool r5_gfx9_vm_lookup(struct r5_vm* vm, uint64 va);
bool r5_gfx9_file_vm_init(struct r5_vm* vm, struct r5_vm_page* pages,
	uint32 capacity);
/* Physical address with the valid bit, the root r5_gfx9_vmid1_setup wrote. */
uint64 r5_gfx9_vmid1_shared_root(void);
bool r5_gfx9_vmid1_map(uint64 va, uint64 pte);
bool r5_gfx9_vmid1_flush();
/* Page-table base for the ring flush: physical address with the valid bit. */
void r5_gfx9_vmid1_set_root(uint64 pd);
/* Every successful map, including a cleared PTE, raises this. */
void r5_gfx9_vmid1_touch();
/* True when a user job must prefix the ring VM flush. pd receives the
 * page-table base, physical address with the valid bit. */
bool r5_gfx9_vmid1_flush_due(uint64* pd);
void r5_gfx9_vmid1_flushed();
bool r5_gfx9_vmid1_lookup(uint64 va);
/* Four entries of va, read through a fresh mapping of each table page. */
void r5_gfx9_vmid1_levels(const char* name, uint64 va);
bool r5_gfx9_vmid1_pte(uint64 va, uint64* pte);
bool r5_gfx9_vmid1_read(uint64 va, uint32* words, uint32 count);
/* Copies up to count dwords, stopping at an unmapped page. Kernel
 * mappings only. Returns how many dwords were copied. */
uint32 r5_gfx9_vmid1_copy(uint64 va, uint32* words, uint32 count);
struct r5_gfx9_ib_now {
	uint64		base;
	uint32		size;
	uint32		offset;
};
void r5_gfx9_ib_regs(r5_gfx9_ib_now* ib1, r5_gfx9_ib_now* ib2);
bool r5_gfx9_take_priv(r5_gfx9_ib_now* ib1, r5_gfx9_ib_now* ib2);
/* gc_9_0_offset.h mmCP_ROQ_IB1_STAT, segment 0, index 0x01e1. */
uint32 r5_gfx9_roq_ib1();
/* ME and PFP header dumps, instruction pointers, stalled stats.
 * Segment 0. Eight header reads: the register can be a FIFO. */
void r5_gfx9_pipe_log(const char* why);
/* How many CP privilege interrupts this boot. IRQ writes it. */
int32 r5_gfx9_priv_count();
/* Kernel mapping only. Makes a stage IB visible to the GPU. */
void r5_gfx9_stage_bytes(uint8* cpu, uint32 bytes);
bool r5_gfx9_k2_gart_page(uint64* mc, uint8** cpu);
/* radeon_hd setting gfx9_wc_gtt. Off unless that line says true. */
bool r5_gfx9_wc_gtt();
int32 r5_gfx9_eop_sem();
status_t r5_gfx9_alloc_vram(uint64 bytes, uint64 align, uint64* offset,
	uint8** address);
status_t r5_gfx9_alloc_user_vram(uint64 bytes, uint64* offset);
void r5_gfx9_free_user_vram(uint64 offset);
/* VMID 1 and later. The pointer is the carveout window, not the BAR. */
uint8* r5_gfx9_vram_cpu(uint64 offset);
void r5_gfx9_ih_counts(uint32* msi, uint32* early, uint32* eop);
/* The BAR window. The K sequence and the HDP probe write use it. */
uint8* r5_gfx9_vram_bar(uint64 offset);
void r5_gfx9_copy_firmware(uint32* version, uint32* feature, uint32 count);
/* gfx9_flush_fence. Off unless the radeon_hd setting says so. */
void r5_gfx9_set_flush_fence(int on);
/* gfx9_cond_exec. Off unless the radeon_hd setting says so. */
void r5_gfx9_set_cond_exec(int on);
/* gfx9_pasid_map. Off unless the radeon_hd setting says so. */
void r5_gfx9_set_pasid_map(int on);
/* gfx9_vmid_per_file. The LUT packet follows the VM flush. */
void r5_gfx9_set_file_pasid(int on);
/* gfx9_ce_meta. Off unless the radeon_hd setting says so. */
void r5_gfx9_set_ce_meta(int on);
int r5_gfx9_ce_meta_on(void);
/* gfx9_gds_switch. Off unless the radeon_hd setting says so. */
void r5_gfx9_set_gds_switch(int on);
/* The open file's PASID. 0 is a kernel job and writes no LUT entry. */
void r5_gfx9_set_job_pasid(uint32 pasid, uint64 file);
/* gfx9_deferred_free. The kernel registers the stamp. The accelerant
 * leaves it unset. */
void r5_gfx9_set_defer_stamp(void (*fn)(uint64 sequence));
void r5_gfx9_set_inv_range(int on);
void r5_gfx9_set_hub_linux(int on);
/* gfx9_hub_linux writes, from prepare_hw, before the ring runs. */
typedef void (*r5_gfx9_hub_fn)(uint64 fbStart, uint64 fbEnd,
	uint32 lowNow, uint32 highNow);
void r5_gfx9_set_hub_aperture(int on);
/* 1 when the device table says the aperture high adds 1. */
void r5_gfx9_set_aperture_plus(int plus);
void r5_gfx9_set_hub_program(r5_gfx9_hub_fn fn);
void r5_gfx9_hub_program(uint64 fbStart, uint64 fbEnd, uint32 lowNow,
	uint32 highNow);
void r5_gfx9_set_vmids(int on);
int r5_gfx9_inv_range_on(void);
int r5_gfx9_hub_linux_on(void);
int r5_gfx9_vmids_on(void);
int r5_gfx9_flush_fence_on(void);
int r5_gfx9_cond_exec_on(void);
int r5_gfx9_gds_switch_on(void);
int r5_gfx9_pasid_map_on(void);
/* -1 leaves that setting. 0 restores the K2 values. 1 programs them. */
void r5_gfx9_switch_registers(int inv, int hub, int vmids);
/* Read-only print of the invalidation ranges, the L1 TLB, AGP, the
 * system aperture, and the live settings. */
void r5_gfx9_log_state(const char* when, char* line, size_t bytes);
/* The file last written into the IH LUT for this VMID. */
uint64 r5_gfx9_file_for_vmid(uint32 vmid);
uint32 r5_gfx9_pasid_for_vmid(uint32 vmid);
/* One queued page fault. 0 when the queue is empty. The interrupt
 * handler only writes the queue. */
int r5_gfx9_fault_take(uint32* pasid, uint32* vmid, uint64* addr);
/* A submit waited because the ring had no room. The count is for the log. */
void r5_gfx9_ring_waited(void);
/* waits, highest used dwords, and the ring length. Does not submit. */
void r5_gfx9_ring_fill(uint32* waits, uint32* maxFill, uint32* dwords);

#endif
