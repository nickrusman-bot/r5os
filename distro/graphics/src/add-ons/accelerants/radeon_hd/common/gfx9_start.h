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
};

bool r5_gfx9_ring_ready();
bool r5_gfx9_user_room(uint32 dwords);
/* Dwords this user job will append, including the 256-dword pad. */
uint32 r5_gfx9_user_commit_dwords(uint32 count);
uint32 r5_gfx9_user_commit_switch(uint32 count, int vmFlush);
/* Continuing write count, and the CP read pointer as a ring offset. */
void r5_gfx9_ring_ptrs(uint64* wptr, uint64* rptrOffset);
bool r5_gfx9_fence_offset(uint64* offset);
uint64 r5_gfx9_user_fence_mc();
bool r5_gfx9_user_submit(const r5_gfx9_user_ib* ibs, uint32 count,
	uint64* _sequence);
/* vmFlush writes the page-table base and the pipeline sync in front of
 * the unchanged body. load 1 uses the context-load bits, 0 keeps them,
 * and -1 uses the single global first-job flag. */
bool r5_gfx9_user_submit_switch(const r5_gfx9_user_ib* ibs, uint32 count,
	uint64* _sequence, int vmFlush, uint64 pd, int load);
uint64 r5_gfx9_user_seen();
uint64 r5_gfx9_k2_word_mc();
bool r5_gfx9_stage_ib_store(uint8* cpu, uint64 destVa);
bool r5_gfx9_stage_ib(uint8* cpu, uint64 destVa);
void r5_gfx9_cp_dump(const char* why, uint64 pte);
bool r5_gfx9_vmid1_setup();
/* One VMID's SH_MEM, the same three writes as the VMID 1..15 loop in
 * r5_gfx9_vmid1_setup, then GRBM_GFX_CNTL 0. That loop is unchanged.
 * Nothing on the K path calls this. */
bool r5_gfx9_sh_mem(uint32 vmid);
void* r5_gfx9_as_open(void);
void r5_gfx9_as_close(void* as, uint64 seen);
void r5_gfx9_as_bind(void* as);
int r5_gfx9_as_reserve(void* as, uint32* vmid, int* flush, uint64* pd);
void r5_gfx9_as_cancel(void);
void r5_gfx9_as_finish(void* as, uint32 vmid, uint64 sequence);
int r5_gfx9_as_first(void* as);
uint32 r5_gfx9_as_vmid(void* as);
bool r5_gfx9_as_lost(void* as);
void r5_gfx9_as_fault(uint32 vmid, uint64 addr);
void r5_gfx9_as_retired(uint64 seen);
int r5_gfx9_mem_limits(uint64* vramCap, uint64* gttCap);
bool r5_gfx9_vmid1_map(void* as, uint64 va, uint64 pte);
bool r5_gfx9_tlb_flush_vmid(uint32 vmid);
bool r5_gfx9_vmid1_flush();
bool r5_gfx9_vmid1_lookup(void* as, uint64 va);
bool r5_gfx9_vmid1_pte(void* as, uint64 va, uint64* pte);
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
int32 r5_gfx9_eop_sem();
status_t r5_gfx9_alloc_vram(uint64 bytes, uint64 align, uint64* offset,
	uint8** address);
status_t r5_gfx9_alloc_user_vram(uint64 bytes, uint64* offset);
void r5_gfx9_free_user_vram(uint64 offset);
void r5_gfx9_copy_firmware(uint32* version, uint32* feature, uint32 count);

#endif
