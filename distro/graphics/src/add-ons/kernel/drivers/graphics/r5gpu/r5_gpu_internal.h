/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * State shared by the GPU core and a vendor backend. No AMD register type.
 */

#ifndef R5_GPU_INTERNAL_H
#define R5_GPU_INTERNAL_H

#include "r5_gpu_backend.h"
#include "r5_pasid.h"
#include "r5_vm.h"

#include <condition_variable.h>
#include <lock.h>

#include <SupportDefs.h>

static const uint32 kMaxBo = 256;
static const uint32 kMaxClientBo = 256;
static const uint32 kMaxSync = 128;
static const uint32 kMaxRange = 8;
static const uint32 kMaxJob = 64;
static const uint32 kMaxList = 8;
static const uint32 kMaxPrt = 32;
static const uint64 kMaxBoBytes = 64ull * 1024ull * 1024ull;
static const uint64 kRoomWaitUs = 100000;
static const uint64 kFreeWaitUs = 5000000;
/* amdgpu.lockup_timeout for GFX is 10000 ms. Client waits do not use this. */
static const bigtime_t kWatchdogUs = 10000000;


struct gpu_client;

struct gpu_sync {
	bool			live;
	volatile uint64	point;
	volatile uint64	submitted;
	gpu_client*		owner;
};


struct gpu_range {
	bool		live;
	uint64		va;
	uint64		offset;
	uint64		bytes;
	/* The tree this range was mapped in. Unmap uses this, not a global. */
	r5_vm*		vm;
};


/* The page table outlives the client while a parked buffer still
 * points at it. refs is the client plus each parked buffer. */
struct r5_file_tree {
	r5_vm			vm;
	r5_vm_page*		pages;
	uint32			pagesN;
	uint32			owner;
	uint64			seen;
	r5_file_count	count;
};


struct gpu_bo {
	bool			live;
	uint32			domain;
	uint32			flags;
	uint64			bytes;
	uint64			vramOffset;
	area_id			area;
	team_id			mapTeam;
	area_id			kernelArea;
	void*			kernel;
	gpu_client*		owner;
	bool			parked;
	uint64			parkSequence;
	/* One hold on the file tree, taken when this buffer is parked. */
	r5_file_tree*	hold;
	/* The framebuffer. Release unmaps it and does not free the pages. */
	bool			borrowed;
	gpu_range		ranges[kMaxRange];
};


struct gpu_job {
	bool			live;
	uint64			sequence;
	bigtime_t		submitted;
	uint32			count;
	gpu_sync*		syncs[kMaxList];
	uint64			points[kMaxList];
	gpu_client*		owner;
};


struct gpu_prt {
	bool			live;
	uint64			va;
	uint64			bytes;
	r5_vm*			vm;
};


struct gpu_client {
	void*			info;
	uint32			handles[kMaxClientBo];
	uint32			handleCount;
	int32			contexts;
	uint64			lastSequence;
	/* Linux amdgpu_ctx.preamble_presented, for this client's one context. */
	int32			preamblePresented;
	/* Handed out at CTX create. 0 means there is no live context. */
	uint64			fenceCtx;
	uint32			pasid;
	/* Own page table when gfx9_vmid_per_file is on. Off leaves this empty. */
	r5_file_tree*	tree;
	/* Set by the retire thread when this file's PASID faults. */
	bool			faulted;
	gpu_client*		next;
	gpu_sync		syncs[kMaxSync];
};


extern int sErrorLines;
extern int sOnceBits;
extern int sDebugOn;
extern int sSelftestOn;
extern int sDeferred;
extern gpu_bo sBo[kMaxBo];
extern gpu_job sJobs[kMaxJob];
extern uint64 sHighestSequence;
extern uint64 sLastIbVa;
extern uint64 sLastIbList[8];
extern uint32 sLastIbDw[8];
extern uint32 sLastIbN;
extern void* sInfo;
extern lock sGpuLock;
extern ConditionVariable sEop;
extern bool sReady;
extern bool sLost;
extern gpu_client* sGuilty;
extern int32 sContexts;
extern r5_pasid_ida sPasids;
extern gpu_client* sClientList;

void gpu_cache_log_settings();
bool gpu_debug_enabled();
bool gpu_dump_begin();
bool gpu_dump_line();
bool gpu_render_enabled();
bool gpu_selftest_enabled();
void gpu_lock();
void gpu_unlock();
void gpu_lost(void* info, const char* why);
status_t gpu_refuse(const char* op, const char* why, status_t status,
	uint32 handle, uint32 domain, uint64 bytes, uint64 va, uint64 offset,
	uint64 mapBytes, uint32 flags);
status_t gpu_copy_in(void* buffer, size_t length, void* local, size_t bytes);
status_t gpu_copy_out(void* buffer, size_t length, const void* local,
	size_t bytes);
void gpu_defer_stamp(uint64 sequence);
gpu_bo* gpu_bo_get(gpu_client* client, uint32 handle);
void gpu_bo_hold_tree(gpu_bo* bo);
void gpu_bo_release(gpu_bo* bo);
void r5_file_tree_drop(r5_file_tree* tree);
uint32 gpu_known_dw(uint64 va);

#endif
