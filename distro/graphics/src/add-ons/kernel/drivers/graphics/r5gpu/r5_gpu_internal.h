/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * State shared by the GPU core and the AMD backend. No AMD header.
 */

#ifndef R5_GPU_INTERNAL_H
#define R5_GPU_INTERNAL_H

#include "r5_gpu_backend.h"

#include <condition_variable.h>
#include <lock.h>

#include <SupportDefs.h>

struct radeon_info;

static const uint32 kMaxBo = 256;
static const uint32 kMaxClientBo = 256;
static const uint32 kMaxSync = 128;
static const uint32 kMaxRange = 8;
static const uint32 kMaxJob = 64;
static const uint32 kMaxList = 8;
static const uint32 kMaxPrt = 32;
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
	void*			space;
	bool			parked;
	uint64			parkSequence;
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
	void*			space;
	uint64			va;
	uint64			bytes;
};


struct gpu_client {
	radeon_info*	info;
	uint32			handles[kMaxClientBo];
	uint32			handleCount;
	int32			contexts;
	uint64			lastSequence;
	gpu_sync		syncs[kMaxSync];
	void*			address;
	uint64			vramUsed;
	uint64			gttUsed;
};


extern gpu_bo sBo[kMaxBo];
extern gpu_job sJobs[kMaxJob];
extern gpu_prt sPrt[kMaxPrt];
extern uint64 sHighestSequence;
extern uint64 sLastIbVa;
extern uint64 sLastIbList[8];
extern uint32 sLastIbDw[8];
extern uint32 sLastIbN;
extern radeon_info* sInfo;
extern bool sReplacing;
extern lock sGpuLock;
extern ConditionVariable sEop;
extern bool sReady;
extern bool sLost;
extern int32 sContexts;
extern sem_id sRetireSem;
extern thread_id sRetireThread;
extern int sErrorLines;
extern int sOnceBits;

bool gpu_debug_enabled();
bool gpu_error_line();
bool gpu_render_enabled();
bool gpu_selftest_enabled();
void gpu_lock();
void gpu_unlock();
void gpu_lost(radeon_info* info, const char* why);

#endif
