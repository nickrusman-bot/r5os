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
};

bool r5_gfx9_ring_ready();
bool r5_gfx9_user_room(uint32 dwords);
bool r5_gfx9_fence_offset(uint64* offset);
uint64 r5_gfx9_user_fence_mc();
bool r5_gfx9_user_submit(const r5_gfx9_user_ib* ibs, uint32 count,
	uint64* _sequence);
uint64 r5_gfx9_user_seen();
bool r5_gfx9_vmid1_setup();
bool r5_gfx9_vmid1_map(uint64 va, uint64 pte);
bool r5_gfx9_vmid1_flush();
bool r5_gfx9_vmid1_lookup(uint64 va);
int32 r5_gfx9_eop_sem();
status_t r5_gfx9_alloc_vram(uint64 bytes, uint64 align, uint64* offset,
	uint8** address);
status_t r5_gfx9_alloc_user_vram(uint64 bytes, uint64* offset);
void r5_gfx9_free_user_vram(uint64 offset);
void r5_gfx9_copy_firmware(uint32* version, uint32* feature, uint32 count);

#endif
