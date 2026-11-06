/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host test of the AMDGPU ioctl translator against a fake R5 kernel.
 */
#include "r5_gpu.h"
#include "r5_translate.h"

#include <amdgpu.h>
#include <amdgpu_drm.h>
#include <xf86drm.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int ac_drm_replay(int fd);


static int
host_err(int err)
{
	return err < 0 ? err : -err;
}


static int gMapFault;
static int gFail;
static int gCalls;
static int gShort;
static uint32_t gReadOffset;
static uint32_t gBoDomain;
static uint64_t gVa;
static uint64_t gBoOffset;
static uint64_t gMapBytes;
static uint32_t gIbCount;
static uint32_t gWaitCount;
static uint32_t gSignalCount;
static uint64_t gIbVa;
static uint32_t gIbDwords;
static uint32_t gWaitHandle;
static uint64_t gWaitPoint;
static int gClones;
static int gCloneFreed;
static int gCurrentTeam = 1;
static int gForceKernelClone;
static uint64_t gSequence;
static uint32_t gCsContext;
static uint32_t gSyncPeak;
static uint32_t gBoPeak;
static int gEnosys;
static int gDeferRetire;
static int gJobAgeUs;
static int gWatch;
static int gLost;
static uint64_t gPageVa;
static uint64_t gPageBytes;
static uint64_t gPagePte;
static int gPageEmpty;
static int gStrictVa;

struct host_job {
	int		live;
	uint64_t	sequence;
	uint32_t	count;
	uint32_t	handle[8];
	uint64_t	point[8];
};

static struct host_job gJob;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "fail %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		gFail++; \
	} \
} while (0)

static void *gMem[256];
static size_t gBytes[256];
static uint32_t gDomain[256];
static uint64_t gVramUsed;

#define HOST_BO_SLOTS 256u
#define HOST_MAX_BO (64ull << 20)
#define HOST_VRAM_POOL 0xf817000ull
static void *gArea[32];
static int gAreaLive[32];
static int gAreaTeam[32];
static int gBoArea[256];
static int gBoTeam[256];
static uint64_t gSyncPoint[128];
static uint64_t gSyncSubmitted[128];
static int gSyncLive[128];


void
r5_host_retire(void)
{
	uint32_t i;
	if (!gJob.live)
		return;
	for (i = 0; i < gJob.count; i++) {
		uint32_t handle = gJob.handle[i];
		if (handle == 0 || handle > 128 || !gSyncLive[handle - 1])
			continue;
		if (gJob.point[i] > gSyncPoint[handle - 1])
			gSyncPoint[handle - 1] = gJob.point[i];
	}
	gJob.live = 0;
}


static int
host_signalled(uint64_t value, uint64_t submitted, uint64_t target, int binary)
{
	if (binary)
		return submitted > 0 && value >= submitted;
	return value >= target;
}


static int
host_blocks(uint32_t handle, uint64_t target, int binary)
{
	uint32_t n;
	if (!gJob.live || handle == 0 || handle > 128)
		return 0;
	for (n = 0; n < gJob.count; n++) {
		if (gJob.handle[n] != handle)
			continue;
		if (binary)
			return 1;
		if (gJob.point[n] >= target && gSyncPoint[handle - 1] < target)
			return 1;
	}
	return 0;
}


static void
host_signal_binary(uint32_t index)
{
	uint64_t stamp = gSyncSubmitted[index];
	uint64_t high = gSequence == 0 ? 1 : gSequence;
	if (gSyncPoint[index] > stamp)
		stamp = gSyncPoint[index];
	if (high > stamp)
		stamp = high;
	gSyncPoint[index] = stamp;
	gSyncSubmitted[index] = stamp;
}

void *
r5_area_address(int area)
{
	if (area <= 0 || area >= 32 || !gAreaLive[area])
		return NULL;
	if (gAreaTeam[area] != gCurrentTeam)
		return NULL;
	return gArea[area];
}

int
r5_area_destroy(int area)
{
	if (area <= 0 || area >= 32 || !gAreaLive[area])
		return -1;
	gAreaLive[area] = 0;
	gArea[area] = NULL;
	gAreaTeam[area] = 0;
	gCloneFreed++;
	return 0;
}


struct host_span {
	uint64_t	address;
	uint64_t	size;
};


/* Haiku __get_memory_map_haiku returns B_OK and ends the table with size 0. */
static int
host_get_memory_map(struct host_span *table, int slots)
{
	if (slots > 0) {
		table[0].address = 0;
		table[0].size = 0;
	}
	return 0;
}


/* 0 is B_OK. 1 is B_BUFFER_OVERFLOW. `pieces` is the number of 4 KiB pieces.
 * One piece covers the whole range.
 */
static int
host_get_memory_map_etc(size_t bytes, struct host_span *table, unsigned *count,
	unsigned pieces)
{
	unsigned room = *count;
	unsigned n;
	unsigned i;
	size_t left = bytes;
	if (room == 0 || pieces == 0)
		return -1;
	n = pieces < room ? pieces : room;
	for (i = 0; i < n && left > 0; i++) {
		size_t take = pieces == 1 ? left : 4096;
		if (take > left)
			take = left;
		table[i].address = 0x2000ull + (uint64_t)i * 4096ull;
		table[i].size = take;
		left -= take;
	}
	*count = i;
	return left > 0 ? 1 : 0;
}


/* old_api is Haiku get_memory_map. The fixed path loops on overflow. */
static int
host_cover(int old_api, size_t bytes, unsigned pieces)
{
	size_t filled = 0;
	if (old_api) {
		struct host_span table[8];
		host_get_memory_map(table, 8);
		return table[0].size == 0 ? 0 : -1;
	}
	while (filled < bytes) {
		struct host_span table[256];
		unsigned count = 256;
		int status = host_get_memory_map_etc(bytes - filled, table, &count,
			pieces);
		size_t step = 0;
		unsigned i;
		if (status < 0)
			return -1;
		for (i = 0; i < count && filled < bytes; i++) {
			uint64_t chunk = table[i].size;
			while (chunk >= 4096 && filled < bytes) {
				chunk -= 4096;
				filled += 4096;
				step += 4096;
			}
		}
		if (step == 0 || status == 0)
			break;
		if (pieces > count)
			pieces -= count;
		else
			pieces = 1;
	}
	return (int)(filled / 4096);
}

int
r5_host_ioctl(int fd, unsigned long op, void *buf, size_t bytes)
{
	(void)fd;
	gCalls++;
	if (bytes == 0) {
		gShort++;
		return host_err(EINVAL);
	}
	if (op == R5_GPU_GET_INFO) {
		if (bytes < sizeof(r5_gpu_info) + sizeof(r5_gpu_amd_info))
			return host_err(EINVAL);
		r5_gpu_info *info = buf;
		r5_gpu_amd_info *amd = (r5_gpu_amd_info *)((unsigned char *)buf
			+ sizeof(*info));
		memset(info, 0, sizeof(*info));
		memset(amd, 0, sizeof(*amd));
		info->size = (uint32_t)sizeof(*info);
		info->version = 1;
		info->api_major = 1;
		info->api_minor = 0;
		info->vendor = 0x1002;
		info->device = 0x15e7;
		info->capabilities = R5_GPU_CAP_RENDER;
		info->vram_bytes = 256ull << 20;
		info->gtt_bytes = 256ull << 20;
		info->va_offset = R5_GPU_VA_OFFSET;
		info->va_max = R5_GPU_VA_MAX;
		info->high_va_offset = R5_GPU_HIGH_VA_OFFSET;
		info->high_va_max = R5_GPU_HIGH_VA_MAX;
		info->vendor_block_size = (uint32_t)sizeof(*amd);
		info->vendor_block_version = 1;
		info->bus = 5;
		info->dev = 0;
		info->func = 0;
		info->subsystem_vendor = 0x1002;
		info->subsystem_device = 0x15e7;
		info->vram_usage = 0x3000;
		info->visible_vram_usage = 0x3000;
		info->gtt_usage = 0x4000;
		amd->size = (uint32_t)sizeof(*amd);
		amd->version = 1;
		amd->device.device_id = 0x15e7;
		amd->device.external_rev = 0xa3;
		amd->device.family = R5_AMDGPU_FAMILY_RV;
		amd->device.ids_flags = R5_AMDGPU_IDS_FLAGS_FUSION;
		amd->device.cu_active_number = 7;
		amd->device.virtual_address_offset = R5_GPU_VA_OFFSET;
		amd->device.virtual_address_max = R5_GPU_VA_MAX;
		amd->device.virtual_address_alignment = 4096;
		amd->device.high_va_offset = R5_GPU_HIGH_VA_OFFSET;
		amd->device.high_va_max = R5_GPU_HIGH_VA_MAX;
		amd->firmware[AMDGPU_INFO_FW_GFX_ME].ver = 0x11;
		amd->firmware[AMDGPU_INFO_FW_GFX_ME].feature = 0x22;
		amd->hw_ip[AMDGPU_HW_IP_GFX].hw_ip_version_major = 9;
		amd->hw_ip[AMDGPU_HW_IP_GFX].hw_ip_version_minor = 3;
		amd->hw_ip[AMDGPU_HW_IP_GFX].ib_start_alignment = 32;
		amd->hw_ip[AMDGPU_HW_IP_GFX].ib_size_alignment = 4;
		amd->hw_ip[AMDGPU_HW_IP_GFX].available_rings = 1;
		amd->hw_ip[AMDGPU_HW_IP_COMPUTE] = amd->hw_ip[AMDGPU_HW_IP_GFX];
		amd->hw_ip[AMDGPU_HW_IP_COMPUTE].available_rings = 0;
		amd->firmware[AMDGPU_INFO_FW_GFX_PFP].ver = 0xc5;
		amd->firmware[AMDGPU_INFO_FW_GFX_PFP].feature = 0x36;
		amd->firmware[AMDGPU_INFO_FW_GFX_MEC].ver = 0x1e3;
		amd->firmware[AMDGPU_INFO_FW_GFX_MEC].feature = 0x36;
		return 0;
	}
	if (op == R5_GPU_READ_REG) {
		if (bytes < sizeof(r5_gpu_read_reg))
			return host_err(EINVAL);
		r5_gpu_read_reg *reg = buf;
		gReadOffset = reg->offset;
		if (reg->offset != 0x063eu)
			return host_err(EPERM);
		reg->value = 0x24000042u;
		return 0;
	}
	if (op == R5_GPU_CTX_CREATE) {
		if (bytes < sizeof(r5_gpu_ctx))
			return host_err(EINVAL);
		((r5_gpu_ctx *)buf)->context = 1;
		return 0;
	}
	if (op == R5_GPU_CTX_DESTROY)
		return bytes < sizeof(r5_gpu_ctx) ? host_err(EINVAL) : 0;
	if (op == R5_GPU_BO_CREATE) {
		if (bytes < sizeof(r5_gpu_bo))
			return host_err(EINVAL);
		r5_gpu_bo *bo = buf;
		if (bo->bytes == 0 || bo->bytes > HOST_MAX_BO)
			return host_err(EINVAL);
		if (bo->domain != R5_GPU_DOMAIN_VRAM && bo->domain != R5_GPU_DOMAIN_GTT)
			return host_err(EINVAL);
		uint64_t rounded = (bo->bytes + 4095ull) & ~4095ull;
		if (bo->domain == R5_GPU_DOMAIN_VRAM
			&& gVramUsed + rounded > HOST_VRAM_POOL)
			return host_err(ENOMEM);
		for (uint32_t i = 0; i < HOST_BO_SLOTS; i++) {
			if (gMem[i] != NULL)
				continue;
			gMem[i] = calloc(1, (size_t)rounded);
			if (gMem[i] == NULL)
				return host_err(ENOMEM);
			gBytes[i] = (size_t)rounded;
			gDomain[i] = bo->domain;
			if (bo->domain == R5_GPU_DOMAIN_VRAM)
				gVramUsed += rounded;
			bo->handle = i + 1;
			gBoDomain = bo->domain;
			if (i + 1u > gBoPeak)
				gBoPeak = i + 1u;
			return 0;
		}
		return host_err(ENOMEM);
	}
	if (op == R5_GPU_BO_FREE) {
		r5_gpu_bo *bo = buf;
		if (bo->handle == 0 || bo->handle > HOST_BO_SLOTS)
			return host_err(EINVAL);
		if (gDomain[bo->handle - 1] == R5_GPU_DOMAIN_VRAM
			&& gVramUsed >= gBytes[bo->handle - 1])
			gVramUsed -= gBytes[bo->handle - 1];
		free(gMem[bo->handle - 1]);
		gMem[bo->handle - 1] = NULL;
		gBytes[bo->handle - 1] = 0;
		gDomain[bo->handle - 1] = 0;
		if (gBoArea[bo->handle - 1] > 0)
			r5_area_destroy(gBoArea[bo->handle - 1]);
		gBoArea[bo->handle - 1] = 0;
		gBoTeam[bo->handle - 1] = 0;
		return 0;
	}
	if (op == R5_GPU_BO_MAP) {
		if (bytes < sizeof(r5_gpu_bo))
			return host_err(EINVAL);
		r5_gpu_bo *bo = buf;
		if (gMapFault == 1) {
			bo->area = -1;
			return 0;
		}
		if (gMapFault == 2) {
			bo->area = 7;
			return 0;
		}
		if (bo->handle == 0 || bo->handle > HOST_BO_SLOTS
			|| gMem[bo->handle - 1] == NULL)
			return host_err(ENOENT);
		if (gBoArea[bo->handle - 1] > 0) {
			if (gBoTeam[bo->handle - 1] != gCurrentTeam)
				return host_err(EPERM);
			bo->area = gBoArea[bo->handle - 1];
			return 0;
		}
		for (int i = 1; i < 32; i++) {
			if (gAreaLive[i])
				continue;
			gArea[i] = gMem[bo->handle - 1];
			gAreaLive[i] = 1;
			/* clone_area lands in the kernel team. vm_clone_area uses
			 * the calling team. gForceKernelClone selects the old path.
			 */
			gAreaTeam[i] = gForceKernelClone ? 0 : gCurrentTeam;
			gBoArea[bo->handle - 1] = i;
			gBoTeam[bo->handle - 1] = gAreaTeam[i];
			bo->area = i;
			gClones++;
			return 0;
		}
		return host_err(ENOMEM);
	}
	if (op == R5_GPU_VA_MAP || op == R5_GPU_VA_UNMAP
		|| op == R5_GPU_VA_REPLACE || op == R5_GPU_VA_CLEAR) {
		if (bytes < sizeof(r5_gpu_va))
			return host_err(EINVAL);
		r5_gpu_va *va = buf;
		uint64_t end = va->gpu_va + va->map_bytes;
		int low = end >= va->gpu_va && va->gpu_va >= R5_GPU_VA_OFFSET
			&& end <= R5_GPU_VA_MAX;
		int high = end >= va->gpu_va && va->gpu_va >= R5_GPU_HIGH_VA_OFFSET
			&& end <= R5_GPU_HIGH_VA_MAX;
		if (va->map_bytes == 0 || (va->map_bytes & 0xfff) != 0
			|| (!low && !high))
			return host_err(EINVAL);
		if (op == R5_GPU_VA_CLEAR) {
			gPageVa = va->gpu_va;
			gPageBytes = va->map_bytes;
			gPagePte = 0;
			gPageEmpty = 1;
			gVa = va->gpu_va;
			gMapBytes = va->map_bytes;
			return 0;
		}
		if ((op == R5_GPU_VA_MAP || op == R5_GPU_VA_REPLACE)
			&& va->handle == 0) {
			if ((va->flags & R5_GPU_VA_PRT) == 0)
				return host_err(EINVAL);
			gPageVa = va->gpu_va;
			gPageBytes = va->map_bytes;
			/* gfx9_check.h kR5Gfx9PtePrt, Linux AMDGPU_PTE_PRT bit 51. */
			gPagePte = 1ull << 51;
			gPageEmpty = 0;
			gVa = va->gpu_va;
			gMapBytes = va->map_bytes;
			return 0;
		}
		if (op == R5_GPU_VA_UNMAP && gPageEmpty && va->gpu_va == gPageVa)
			return host_err(EINVAL);
		if (va->handle == 0 || va->handle > HOST_BO_SLOTS
			|| gMem[va->handle - 1] == NULL)
			return host_err(EINVAL);
		if (va->bo_offset > gBytes[va->handle - 1]
			|| va->map_bytes > gBytes[va->handle - 1] - va->bo_offset)
			return host_err(EINVAL);
		if (gStrictVa && op == R5_GPU_VA_MAP && !gPageEmpty
			&& gPageBytes != 0
			&& va->gpu_va < gPageVa + gPageBytes
			&& va->gpu_va + va->map_bytes > gPageVa)
			return host_err(EINVAL);
		if ((op == R5_GPU_VA_MAP || op == R5_GPU_VA_REPLACE)
			&& gDomain[va->handle - 1] == R5_GPU_DOMAIN_GTT) {
			int pages = host_cover(0, (size_t)va->map_bytes, 1);
			if ((uint64_t)pages * 4096 != va->map_bytes)
				return host_err(EIO);
		}
		gVa = va->gpu_va;
		gBoOffset = va->bo_offset;
		gMapBytes = va->map_bytes;
		gPageVa = va->gpu_va;
		gPageBytes = va->map_bytes;
		gPagePte = op == R5_GPU_VA_UNMAP ? 0 : 1ull;
		gPageEmpty = 0;
		return 0;
	}
	if (op == R5_GPU_CS_SUBMIT) {
		if (gLost)
			return host_err(EIO);
		if (gWatch && gJob.live && gJobAgeUs >= 5000000) {
			gLost = 1;
			return host_err(EIO);
		}
		if (bytes < sizeof(r5_gpu_cs))
			return host_err(EINVAL);
		r5_gpu_cs *cs = buf;
		gIbCount = cs->ib_count;
		gWaitCount = cs->wait_count;
		gSignalCount = cs->signal_count;
		gCsContext = cs->context;
		if (cs->context != 1)
			return host_err(EINVAL);
		if (cs->ibs != 0 && cs->ib_count > 0) {
			r5_gpu_ib *ib = (r5_gpu_ib *)(uintptr_t)cs->ibs;
			for (uint32_t i = 0; i < cs->ib_count; i++) {
				if (ib[i].flags != 0)
					return host_err(EINVAL);
			}
			gIbVa = ib->gpu_va;
			gIbDwords = ib->dwords;
		}
		if (cs->waits != 0 && cs->wait_count > 0) {
			r5_gpu_cs_sync *wait = (r5_gpu_cs_sync *)(uintptr_t)cs->waits;
			gWaitHandle = wait->handle;
			gWaitPoint = wait->point;
			for (uint32_t i = 0; i < cs->wait_count; i++) {
				uint32_t handle = wait[i].handle;
				if (handle == 0 || handle > 128 || !gSyncLive[handle - 1])
					continue;
				int ready = host_signalled(gSyncPoint[handle - 1],
					gSyncSubmitted[handle - 1], wait[i].point,
					(wait[i].flags & R5_GPU_CS_SYNC_BINARY) != 0);
				if (!ready)
					return host_err(ETIMEDOUT);
			}
		}
		gSequence = gSequence == 0 ? 11 : gSequence + 1;
		if (gDeferRetire) {
			if (gJob.live)
				return host_err(EBUSY);
			gJob.live = 1;
			gJob.sequence = gSequence;
			gJob.count = 0;
		}
		if (cs->signals != 0 && cs->signal_count > 0) {
			r5_gpu_cs_sync *signal = (r5_gpu_cs_sync *)(uintptr_t)cs->signals;
			for (uint32_t i = 0; i < cs->signal_count; i++) {
				uint32_t handle = signal[i].handle;
				uint64_t point;
				if (handle == 0 || handle > 128 || !gSyncLive[handle - 1])
					continue;
				if ((signal[i].flags & R5_GPU_CS_SYNC_BINARY) != 0
					|| signal[i].point == 0)
					point = gSequence;
				else
					point = signal[i].point;
				if (gDeferRetire) {
					if (gJob.count >= 8)
						return host_err(ENOMEM);
					gJob.handle[gJob.count] = handle;
					gJob.point[gJob.count] = point;
					gJob.count++;
					if (point > gSyncSubmitted[handle - 1])
						gSyncSubmitted[handle - 1] = point;
					continue;
				}
				if ((signal[i].flags & R5_GPU_CS_SYNC_BINARY) != 0
					|| point > gSyncPoint[handle - 1])
					gSyncPoint[handle - 1] = point;
				if (gSyncPoint[handle - 1] > gSyncSubmitted[handle - 1])
					gSyncSubmitted[handle - 1] = gSyncPoint[handle - 1];
			}
		}
		cs->sequence = gSequence;
		return 0;
	}
	if (op == R5_GPU_FENCE_WAIT) {
		if (bytes < sizeof(r5_gpu_fence))
			return host_err(EINVAL);
		r5_gpu_fence *fence = buf;
		if (gLost)
			return host_err(ECANCELED);
		if (gJob.live && fence->sequence == gJob.sequence)
			return host_err(ETIMEDOUT);
		if (gWatch && gJob.live && fence->sequence == gJob.sequence
			&& gJobAgeUs < 5000000)
			return host_err(ETIMEDOUT);
		if (fence->sequence == 0 || fence->sequence > gSequence)
			return host_err(ETIMEDOUT);
		return 0;
	}
	if (op == R5_GPU_SYNCOBJ_CREATE || op == R5_GPU_SYNCOBJ_DESTROY
		|| op == R5_GPU_SYNCOBJ_WAIT || op == R5_GPU_SYNCOBJ_SIGNAL
		|| op == R5_GPU_SYNCOBJ_RESET || op == R5_GPU_SYNCOBJ_QUERY) {
		if (bytes < sizeof(r5_gpu_syncobj))
			return host_err(EINVAL);
		r5_gpu_syncobj *sync = buf;
		if (op == R5_GPU_SYNCOBJ_CREATE) {
			int slot = -1;
			uint32_t live = 0;
			for (uint32_t i = 0; i < 128; i++) {
				if (gSyncLive[i])
					live++;
				else if (slot < 0)
					slot = (int)i;
			}
			if (slot < 0)
				return host_err(ENOMEM);
			gSyncLive[slot] = 1;
			gSyncPoint[slot] = (sync->flags & R5_GPU_SYNCOBJ_SIGNALED) ? 1 : 0;
			gSyncSubmitted[slot] = gSyncPoint[slot];
			sync->handle = (uint32_t)slot + 1u;
			if (live + 1 > gSyncPeak)
				gSyncPeak = live + 1;
			return 0;
		}
		if (sync->handle == 0 || sync->handle > 128 || !gSyncLive[sync->handle - 1])
			return host_err(EINVAL);
		uint32_t index = sync->handle - 1u;
		if (op == R5_GPU_SYNCOBJ_DESTROY) {
			uint32_t n;
			if (gJob.live) {
				for (n = 0; n < gJob.count; n++) {
					if (gJob.handle[n] == sync->handle)
						gJob.handle[n] = 0;
				}
			}
			gSyncLive[index] = 0;
			gSyncPoint[index] = 0;
			gSyncSubmitted[index] = 0;
			return 0;
		}
		if (op == R5_GPU_SYNCOBJ_RESET) {
			gSyncPoint[index] = 0;
			gSyncSubmitted[index] = 0;
			return 0;
		}
		if (op == R5_GPU_SYNCOBJ_QUERY) {
			sync->point = gSyncPoint[index];
			return 0;
		}
		if (op == R5_GPU_SYNCOBJ_SIGNAL) {
			if ((sync->flags & R5_GPU_SYNCOBJ_BINARY) != 0) {
				host_signal_binary(index);
				return 0;
			}
			if (sync->point > gSyncPoint[index])
				gSyncPoint[index] = sync->point;
			if (sync->point > gSyncSubmitted[index])
				gSyncSubmitted[index] = sync->point;
			return 0;
		}
		{
			int binary = (sync->flags & R5_GPU_SYNCOBJ_BINARY) != 0;
			if (binary && gSyncSubmitted[index] == 0)
				return host_err(ETIMEDOUT);
			if (!host_signalled(gSyncPoint[index], gSyncSubmitted[index],
				sync->point, binary))
				return host_err(ETIMEDOUT);
			return 0;
		}
	}
	if (op == R5_GPU_SYNCOBJ_WAIT_LIST) {
		if (gWatch && gJob.live && gJobAgeUs >= 5000000) {
			gLost = 1;
			gJob.live = 0;
			return host_err(ECANCELED);
		}
		if (gLost)
			return host_err(ECANCELED);
		r5_gpu_syncobj_wait *wait = buf;
		uint32_t *handles;
		uint64_t *points;
		uint32_t i;
		int found = -1;
		uint32_t met = 0;
		if (bytes < sizeof(*wait) || wait->count == 0 || wait->handles == 0)
			return host_err(EINVAL);
		handles = (uint32_t *)(uintptr_t)wait->handles;
		points = (uint64_t *)(uintptr_t)wait->points;
		if ((wait->flags & R5_GPU_WAIT_FOR_SUBMIT) == 0) {
			for (i = 0; i < wait->count; i++) {
				uint64_t target = points == NULL ? 0 : points[i];
				int submitted;
				if (handles[i] == 0 || handles[i] > 128
					|| !gSyncLive[handles[i] - 1])
					return host_err(EINVAL);
				submitted = (wait->flags & R5_GPU_WAIT_BINARY) != 0
					? gSyncSubmitted[handles[i] - 1] > 0
					: gSyncSubmitted[handles[i] - 1] >= target;
				if (!submitted)
					return host_err(EINVAL);
			}
		}
		for (i = 0; i < wait->count; i++) {
			uint64_t value;
			uint64_t target;
			int ready;
			if (handles[i] == 0 || handles[i] > 128 || !gSyncLive[handles[i] - 1])
				return host_err(EINVAL);
			value = (wait->flags & R5_GPU_WAIT_AVAILABLE) != 0
				? gSyncSubmitted[handles[i] - 1] : gSyncPoint[handles[i] - 1];
			target = points == NULL ? 0 : points[i];
			if ((wait->flags & R5_GPU_WAIT_AVAILABLE) == 0
				&& host_blocks(handles[i], target,
					(wait->flags & R5_GPU_WAIT_BINARY) != 0))
				ready = 0;
			else
				ready = host_signalled(value, gSyncSubmitted[handles[i] - 1],
					target, (wait->flags & R5_GPU_WAIT_BINARY) != 0);
			if (!ready)
				continue;
			met++;
			if (found < 0)
				found = (int)i;
			if ((wait->flags & R5_GPU_WAIT_ALL) == 0)
				break;
		}
		if ((wait->flags & R5_GPU_WAIT_ALL) != 0 && met == wait->count) {
			wait->first_signaled = 0;
			return 0;
		}
		if ((wait->flags & R5_GPU_WAIT_ALL) == 0 && found >= 0) {
			wait->first_signaled = (uint32_t)found;
			return 0;
		}
		return host_err(ETIMEDOUT);
	}
	if (op == R5_GPU_SYNCOBJ_TRANSFER) {
		if (gLost)
			return host_err(ECANCELED);
		r5_gpu_syncobj_transfer *transfer = buf;
		uint32_t src;
		uint32_t dst;
		uint64_t dstPoint;
		uint32_t n;
		int reached;
		int will = 0;
		if (bytes < sizeof(*transfer) || transfer->flags != 0)
			return host_err(EINVAL);
		src = transfer->src_handle;
		dst = transfer->dst_handle;
		if (src == 0 || dst == 0 || src > 128 || dst > 128
			|| !gSyncLive[src - 1] || !gSyncLive[dst - 1])
			return host_err(ENOENT);
		if (transfer->src_point == 0 && gSyncSubmitted[src - 1] == 0)
			return host_err(EINVAL);
		reached = !host_blocks(src, transfer->src_point,
				transfer->src_point == 0)
			&& host_signalled(gSyncPoint[src - 1], gSyncSubmitted[src - 1],
				transfer->src_point, transfer->src_point == 0);
		if (reached) {
			if (transfer->dst_point == 0)
				host_signal_binary(dst - 1);
			else {
				if (transfer->dst_point > gSyncPoint[dst - 1])
					gSyncPoint[dst - 1] = transfer->dst_point;
				if (transfer->dst_point > gSyncSubmitted[dst - 1])
					gSyncSubmitted[dst - 1] = transfer->dst_point;
			}
			return 0;
		}
		if (gJob.live) {
			uint64_t lowest = 0;
			int have = 0;
			for (n = 0; n < gJob.count; n++) {
				if (gJob.handle[n] != src)
					continue;
				if (transfer->src_point == 0) {
					if (gJob.point[n] == gSyncSubmitted[src - 1])
						will = 1;
				} else if (gJob.point[n] >= transfer->src_point
					&& (!have || gJob.point[n] < lowest)) {
					will = 1;
					have = 1;
					lowest = gJob.point[n];
				}
			}
		}
		if (!will)
			return host_err(EINVAL);
		dstPoint = transfer->dst_point == 0 ? gJob.sequence : transfer->dst_point;
		for (n = 0; n < gJob.count; n++) {
			if (gJob.handle[n] != dst)
				continue;
			if (dstPoint > gJob.point[n])
				gJob.point[n] = dstPoint;
			if (dstPoint > gSyncSubmitted[dst - 1])
				gSyncSubmitted[dst - 1] = dstPoint;
			return 0;
		}
		if (gJob.count >= 8)
			return host_err(ENOMEM);
		gJob.handle[gJob.count] = dst;
		gJob.point[gJob.count] = dstPoint;
		gJob.count++;
		if (dstPoint > gSyncSubmitted[dst - 1])
			gSyncSubmitted[dst - 1] = dstPoint;
		return 0;
	}
	gEnosys++;
	return host_err(ENOSYS);
}

int
drmIoctl(int fd, unsigned long request, void *arg)
{
	size_t bytes = (request >> 16) & 0x3fffu;
	int translated = r5_drm_ioctl(fd, request, arg, bytes);
	if (translated == 1) {
		errno = ENOSYS;
		return -1;
	}
	return translated;
}

int
drmCommandWrite(int fd, unsigned long index, void *data, unsigned long size)
{
	unsigned long request = DRM_IOC(DRM_IOC_WRITE, DRM_IOCTL_BASE,
		DRM_COMMAND_BASE + index, size);
	if (drmIoctl(fd, request, data))
		return -errno;
	return 0;
}

int
drmCommandWriteRead(int fd, unsigned long index, void *data, unsigned long size)
{
	unsigned long request = DRM_IOC(DRM_IOC_READWRITE, DRM_IOCTL_BASE,
		DRM_COMMAND_BASE + index, size);
	if (drmIoctl(fd, request, data))
		return -errno;
	return 0;
}

drmVersionPtr
drmGetVersion(int fd)
{
	struct drm_version version;
	drmVersionPtr out;
	memset(&version, 0, sizeof(version));
	if (drmIoctl(fd, DRM_IOCTL_VERSION, &version))
		return NULL;
	out = calloc(1, sizeof(*out));
	out->version_major = version.version_major;
	out->version_minor = version.version_minor;
	out->version_patchlevel = version.version_patchlevel;
	out->name = calloc(1, version.name_len + 1);
	out->date = calloc(1, version.date_len + 1);
	out->desc = calloc(1, version.desc_len + 1);
	version.name = out->name;
	version.date = out->date;
	version.desc = out->desc;
	if (drmIoctl(fd, DRM_IOCTL_VERSION, &version))
		return NULL;
	out->name_len = (int)version.name_len;
	out->date_len = (int)version.date_len;
	out->desc_len = (int)version.desc_len;
	return out;
}

void
drmFreeVersion(drmVersionPtr version)
{
	if (version == NULL)
		return;
	free(version->name);
	free(version->date);
	free(version->desc);
	free(version);
}

int
drmGetNodeTypeFromFd(int fd)
{
	(void)fd;
	return DRM_NODE_RENDER;
}

int
drmCloseBufferHandle(int fd, uint32_t handle)
{
	struct drm_gem_close args;
	memset(&args, 0, sizeof(args));
	args.handle = handle;
	return drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &args);
}

void
drmMsg(const char *format, ...)
{
	(void)format;
}

char *
drmGetPrimaryDeviceNameFromFd(int fd)
{
	(void)fd;
	return NULL;
}

int
drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
	(void)fd; (void)handle; (void)flags; (void)prime_fd;
	return -1;
}

int
drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
	(void)fd; (void)prime_fd; (void)handle;
	return -1;
}

int
drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
	(void)fd; (void)flags; (void)handle;
	return -1;
}

int
drmSyncobjDestroy(int fd, uint32_t handle)
{
	(void)fd; (void)handle;
	return -1;
}

int
drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd)
{
	(void)fd; (void)handle; (void)obj_fd;
	return -1;
}

int
drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle)
{
	(void)fd; (void)obj_fd; (void)handle;
	return -1;
}

int
drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd)
{
	(void)fd; (void)handle; (void)sync_file_fd;
	return -1;
}

int
drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
	(void)fd; (void)handle; (void)sync_file_fd;
	return -1;
}

int
drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles,
	int64_t timeout_nsec, unsigned flags, uint32_t *first_signaled)
{
	(void)fd; (void)handles; (void)num_handles;
	(void)timeout_nsec; (void)flags; (void)first_signaled;
	return -1;
}

int
drmSyncobjReset(int fd, const uint32_t *handles, uint32_t handle_count)
{
	(void)fd; (void)handles; (void)handle_count;
	return -1;
}

int
drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t handle_count)
{
	(void)fd; (void)handles; (void)handle_count;
	return -1;
}

int
drmSyncobjTimelineSignal(int fd, const uint32_t *handles, uint64_t *points,
	uint32_t handle_count)
{
	(void)fd; (void)handles; (void)points; (void)handle_count;
	return -1;
}

int
drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points,
	unsigned num_handles, int64_t timeout_nsec, unsigned flags,
	uint32_t *first_signaled)
{
	(void)fd; (void)handles; (void)points; (void)num_handles;
	(void)timeout_nsec; (void)flags; (void)first_signaled;
	return -1;
}

int
drmSyncobjQuery(int fd, uint32_t *handles, uint64_t *points, uint32_t handle_count)
{
	(void)fd; (void)handles; (void)points; (void)handle_count;
	return -1;
}

int
drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points,
	uint32_t handle_count, uint32_t flags)
{
	(void)fd; (void)handles; (void)points; (void)handle_count; (void)flags;
	return -1;
}

int
drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point,
	uint32_t src_handle, uint64_t src_point, uint32_t flags)
{
	(void)fd; (void)dst_handle; (void)dst_point;
	(void)src_handle; (void)src_point; (void)flags;
	return -1;
}

void
drmFreeDevice(drmDevicePtr *device)
{
	if (device == NULL || *device == NULL)
		return;
	free(*device);
	*device = NULL;
}


static int
info_query(int fd, uint32_t query, uint32_t hw, void *out, size_t bytes)
{
	struct drm_amdgpu_info info;
	memset(&info, 0, sizeof(info));
	info.return_pointer = (uint64_t)(uintptr_t)out;
	info.return_size = (uint32_t)bytes;
	info.query = query;
	if (query == AMDGPU_INFO_HW_IP_INFO || query == AMDGPU_INFO_HW_IP_COUNT)
		info.query_hw_ip.type = hw;
	else if (query == AMDGPU_INFO_VIDEO_CAPS)
		info.video_cap.type = hw;
	else if (query == AMDGPU_INFO_FW_VERSION)
		info.query_fw.fw_type = hw;
	else if (query == AMDGPU_INFO_READ_MMR_REG) {
		info.read_mmr_reg.dword_offset = hw;
		info.read_mmr_reg.count = 1;
	}
	return drmCommandWriteRead(fd, DRM_AMDGPU_INFO, &info, sizeof(info));
}


static void
submit_zero(int node)
{
	struct drm_syncobj_create created;
	struct drm_syncobj_transfer transfer;
	struct drm_syncobj_wait waited;
	struct drm_amdgpu_cs_chunk_ib ib;
	struct drm_amdgpu_cs_chunk_sem sem;
	struct drm_amdgpu_cs_chunk chunks[2];
	uint64_t chunkPtr[2];
	union drm_amdgpu_cs cs;
	uint32_t queue;
	uint32_t fresh;
	uint32_t running;
	uint32_t dst;

	memset(&created, 0, sizeof(created));
	created.flags = DRM_SYNCOBJ_CREATE_SIGNALED;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
	queue = created.handle;
	memset(&created, 0, sizeof(created));
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
	dst = created.handle;
	memset(&transfer, 0, sizeof(transfer));
	transfer.src_handle = queue;
	transfer.dst_handle = dst;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_TRANSFER, &transfer) == 0);
	memset(&waited, 0, sizeof(waited));
	waited.handles = (uintptr_t)&dst;
	waited.count_handles = 1;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) == 0);

	memset(&created, 0, sizeof(created));
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
	fresh = created.handle;
	memset(&transfer, 0, sizeof(transfer));
	transfer.src_handle = fresh;
	transfer.dst_handle = dst;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_TRANSFER, &transfer) != 0);
	CHECK(errno == EINVAL);

	memset(&created, 0, sizeof(created));
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
	running = created.handle;
	memset(&ib, 0, sizeof(ib));
	ib.va_start = 0x1000;
	ib.ib_bytes = 4;
	ib.ip_type = AMDGPU_HW_IP_GFX;
	memset(&sem, 0, sizeof(sem));
	sem.handle = running;
	memset(chunks, 0, sizeof(chunks));
	chunks[0].chunk_id = AMDGPU_CHUNK_ID_IB;
	chunks[0].length_dw = (uint32_t)(sizeof(ib) / 4);
	chunks[0].chunk_data = (uint64_t)(uintptr_t)&ib;
	chunks[1].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_OUT;
	chunks[1].length_dw = (uint32_t)(sizeof(sem) / 4);
	chunks[1].chunk_data = (uint64_t)(uintptr_t)&sem;
	chunkPtr[0] = (uint64_t)(uintptr_t)&chunks[0];
	chunkPtr[1] = (uint64_t)(uintptr_t)&chunks[1];
	memset(&cs, 0, sizeof(cs));
	cs.in.ctx_id = 1;
	cs.in.num_chunks = 2;
	cs.in.chunks = (uint64_t)(uintptr_t)chunkPtr;
	gDeferRetire = 1;
	CHECK(drmCommandWriteRead(node, DRM_AMDGPU_CS, &cs, sizeof(cs)) == 0);
	gDeferRetire = 0;
	memset(&created, 0, sizeof(created));
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
	dst = created.handle;
	memset(&transfer, 0, sizeof(transfer));
	transfer.src_handle = running;
	transfer.dst_handle = dst;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_TRANSFER, &transfer) == 0);
	memset(&waited, 0, sizeof(waited));
	waited.handles = (uintptr_t)&dst;
	waited.count_handles = 1;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) != 0);
	CHECK(errno == ETIME);
	r5_host_retire();
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) == 0);

	memset(&created, 0, sizeof(created));
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
	fresh = created.handle;
	memset(&waited, 0, sizeof(waited));
	waited.handles = (uintptr_t)&fresh;
	waited.count_handles = 1;
	waited.timeout_nsec = 1000000000LL;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) != 0);
	CHECK(errno == EINVAL);
	waited.flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) != 0);
	CHECK(errno == ETIME);
}


static int
cs_binary(int node, uint32_t signalHandle, uint32_t waitHandle, int defer)
{
	struct drm_amdgpu_cs_chunk_ib ib;
	struct drm_amdgpu_cs_chunk_sem sems[2];
	struct drm_amdgpu_cs_chunk chunks[3];
	uint64_t chunkPtr[3];
	union drm_amdgpu_cs cs;
	uint32_t count = 1;
	memset(&ib, 0, sizeof(ib));
	memset(sems, 0, sizeof(sems));
	memset(chunks, 0, sizeof(chunks));
	ib.va_start = 0x1000;
	ib.ib_bytes = 4;
	ib.ip_type = AMDGPU_HW_IP_GFX;
	chunks[0].chunk_id = AMDGPU_CHUNK_ID_IB;
	chunks[0].length_dw = (uint32_t)(sizeof(ib) / 4);
	chunks[0].chunk_data = (uint64_t)(uintptr_t)&ib;
	chunkPtr[0] = (uint64_t)(uintptr_t)&chunks[0];
	if (waitHandle != 0) {
		sems[0].handle = waitHandle;
		chunks[count].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_IN;
		chunks[count].length_dw = (uint32_t)(sizeof(sems[0]) / 4);
		chunks[count].chunk_data = (uint64_t)(uintptr_t)&sems[0];
		chunkPtr[count] = (uint64_t)(uintptr_t)&chunks[count];
		count++;
	}
	if (signalHandle != 0) {
		sems[1].handle = signalHandle;
		chunks[count].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_OUT;
		chunks[count].length_dw = (uint32_t)(sizeof(sems[1]) / 4);
		chunks[count].chunk_data = (uint64_t)(uintptr_t)&sems[1];
		chunkPtr[count] = (uint64_t)(uintptr_t)&chunks[count];
		count++;
	}
	memset(&cs, 0, sizeof(cs));
	cs.in.ctx_id = 1;
	cs.in.num_chunks = count;
	cs.in.chunks = (uint64_t)(uintptr_t)chunkPtr;
	gDeferRetire = defer;
	int ret = drmCommandWriteRead(node, DRM_AMDGPU_CS, &cs, sizeof(cs));
	gDeferRetire = 0;
	return ret;
}


static void
r5_host_drop_client(void)
{
	gPageEmpty = 1;
	gPagePte = 0;
	gPageBytes = 0;
	gVa = 0;
}


static void
binary_replay(int node)
{
	struct drm_syncobj_create created;
	struct drm_syncobj_transfer transfer;
	struct drm_syncobj_wait waited;
	struct drm_syncobj_array list;
	uint32_t queue;
	uint32_t reused;
	uint32_t dst;

	memset(&created, 0, sizeof(created));
	created.flags = DRM_SYNCOBJ_CREATE_SIGNALED;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
	queue = created.handle;
	CHECK(cs_binary(node, queue, 0, 0) == 0);
	CHECK(cs_binary(node, queue, 0, 1) == 0);
	memset(&created, 0, sizeof(created));
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
	dst = created.handle;
	memset(&transfer, 0, sizeof(transfer));
	transfer.src_handle = queue;
	transfer.dst_handle = dst;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_TRANSFER, &transfer) == 0);
	memset(&waited, 0, sizeof(waited));
	waited.handles = (uintptr_t)&dst;
	waited.count_handles = 1;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) != 0);
	CHECK(errno == ETIME);
	r5_host_retire();
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) == 0);

	memset(&created, 0, sizeof(created));
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
	reused = created.handle;
	CHECK(cs_binary(node, reused, 0, 0) == 0);
	CHECK(cs_binary(node, 0, reused, 0) == 0);
	CHECK(cs_binary(node, reused, 0, 1) == 0);
	CHECK(cs_binary(node, 0, reused, 0) != 0);
	CHECK(errno == ETIMEDOUT);
	r5_host_retire();

	memset(&list, 0, sizeof(list));
	list.handles = (uint64_t)(uintptr_t)&reused;
	list.count_handles = 1;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_RESET, &list) == 0);
	memset(&waited, 0, sizeof(waited));
	waited.handles = (uintptr_t)&reused;
	waited.count_handles = 1;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) != 0);
	CHECK(errno == EINVAL);

	/* A signaled queue sync plus a job whose fence never lands.
	 * The transfer is the empty vkQueueWaitIdle submit. */
	{
		struct drm_amdgpu_cs_chunk_ib ib;
		struct drm_amdgpu_cs_chunk_syncobj signal;
		struct drm_amdgpu_cs_chunk chunks[2];
		uint64_t chunkPtr[2];
		union drm_amdgpu_cs cs;
		union drm_amdgpu_wait_cs fence;
		uint32_t queue;
		uint32_t dst;
		memset(&created, 0, sizeof(created));
		created.flags = DRM_SYNCOBJ_CREATE_SIGNALED;
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
		queue = created.handle;
		memset(&ib, 0, sizeof(ib));
		memset(&signal, 0, sizeof(signal));
		memset(chunks, 0, sizeof(chunks));
		ib.va_start = 0x1000;
		ib.ib_bytes = 4;
		ib.ip_type = AMDGPU_HW_IP_GFX;
		signal.handle = queue;
		signal.point = 0;
		chunks[0].chunk_id = AMDGPU_CHUNK_ID_IB;
		chunks[0].length_dw = (uint32_t)(sizeof(ib) / 4);
		chunks[0].chunk_data = (uint64_t)(uintptr_t)&ib;
		chunks[1].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL;
		chunks[1].length_dw = (uint32_t)(sizeof(signal) / 4);
		chunks[1].chunk_data = (uint64_t)(uintptr_t)&signal;
		chunkPtr[0] = (uint64_t)(uintptr_t)&chunks[0];
		chunkPtr[1] = (uint64_t)(uintptr_t)&chunks[1];
		memset(&cs, 0, sizeof(cs));
		cs.in.ctx_id = 1;
		cs.in.num_chunks = 2;
		cs.in.chunks = (uint64_t)(uintptr_t)chunkPtr;
		gJob.live = 0;
		gDeferRetire = 1;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_CS, &cs, sizeof(cs)) == 0);
		gDeferRetire = 0;
		memset(&created, 0, sizeof(created));
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
		dst = created.handle;
		memset(&transfer, 0, sizeof(transfer));
		transfer.src_handle = queue;
		transfer.dst_handle = dst;
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_TRANSFER, &transfer) == 0);
		memset(&waited, 0, sizeof(waited));
		waited.handles = (uintptr_t)&dst;
		waited.count_handles = 1;
		waited.flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT
			| DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;
		waited.timeout_nsec = 1000000LL;
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) != 0);
		CHECK(errno == ETIME);
		memset(&fence, 0, sizeof(fence));
		fence.in.handle = gSequence;
		fence.in.timeout = 1000000000ull;
		fence.in.ip_type = AMDGPU_HW_IP_GFX;
		fence.in.ctx_id = 1;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_WAIT_CS, &fence,
			sizeof(fence)) == 0);
		CHECK(fence.out.status == 1);
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_DESTROY, &(struct drm_syncobj_destroy){
			.handle = dst }) == 0);
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_DESTROY, &(struct drm_syncobj_destroy){
			.handle = queue }) == 0);
		gWatch = 1;
		gJobAgeUs = 6000000;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_CS, &cs, sizeof(cs)) != 0);
		CHECK(errno == EIO);
		CHECK(gLost == 1);
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) != 0);
		CHECK(errno == ECANCELED);
		gWatch = 0;
		gLost = 0;
		gJob.live = 0;
		gJobAgeUs = 0;
	}
	{
		struct drm_syncobj_create created;
		struct drm_syncobj_wait waited;
		struct drm_syncobj_destroy destroy;
		r5_gpu_bo bo;
		r5_gpu_va va;
		uint32_t handle;
		memset(&created, 0, sizeof(created));
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &created) == 0);
		handle = created.handle;
		gJob.live = 1;
		gJob.count = 1;
		gJob.handle[0] = handle;
		gJob.point[0] = 1;
		gSyncSubmitted[handle - 1] = 1;
		gSyncPoint[handle - 1] = 0;
		gWatch = 1;
		gJobAgeUs = 5000000;
		gLost = 0;
		memset(&waited, 0, sizeof(waited));
		waited.handles = (uintptr_t)&handle;
		waited.count_handles = 1;
		waited.timeout_nsec = INT64_MAX;
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &waited) != 0);
		CHECK(errno == ECANCELED);
		CHECK(gLost == 1);
		CHECK(gJob.live == 0);
		memset(&destroy, 0, sizeof(destroy));
		destroy.handle = handle;
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy) == 0);
		gWatch = 0;
		gLost = 0;
		gJobAgeUs = 0;

		memset(&bo, 0, sizeof(bo));
		bo.bytes = 4096;
		bo.domain = R5_GPU_DOMAIN_GTT;
		CHECK(r5_host_ioctl(0, R5_GPU_BO_CREATE, &bo, sizeof(bo)) == 0);
		r5_host_drop_client();
		gStrictVa = 1;
		memset(&va, 0, sizeof(va));
		va.handle = bo.handle;
		va.gpu_va = 0x100000000ull;
		va.map_bytes = 4096;
		va.flags = R5_GPU_VA_READ | R5_GPU_VA_WRITE;
		CHECK(r5_host_ioctl(0, R5_GPU_VA_MAP, &va, sizeof(va)) == 0);
		CHECK(r5_host_ioctl(0, R5_GPU_VA_MAP, &va, sizeof(va)) != 0);
		r5_host_drop_client();
		CHECK(r5_host_ioctl(0, R5_GPU_VA_MAP, &va, sizeof(va)) == 0);
		r5_host_drop_client();
		gStrictVa = 0;
		CHECK(r5_host_ioctl(0, R5_GPU_BO_FREE, &bo, sizeof(bo)) == 0);
	}
}


static int
radv_replay(int fd)
{
	drmDevicePtr listed[4];
	drmDevicePtr one = NULL;
	int count;
	char dir[] = "/tmp/r5-gpu-replay";
	char path[] = "/tmp/r5-gpu-replay/0";
	int node;
	drmVersionPtr version;
	char name[64];
	char date[64];
	char desc[64];
	struct drm_version raw;
	struct drm_get_cap cap;
	struct drm_amdgpu_memory_info memory;
	struct drm_amdgpu_info_vram_gtt heap;
	struct drm_amdgpu_info_hw_ip ip;
	uint32_t mmr = 0;
	uint32_t handles[2];
	uint64_t points[2];
	struct drm_syncobj_create create;
	struct drm_syncobj_array list;
	struct drm_syncobj_timeline_array timeline;
	struct drm_amdgpu_cs_chunk_ib ib;
	struct drm_amdgpu_cs_chunk_sem sems[2];
	struct drm_amdgpu_cs_chunk_syncobj syncs[2];
	struct drm_amdgpu_cs_chunk_dep dep;
	struct drm_amdgpu_cs_chunk chunks[4];
	union drm_amdgpu_wait_cs wait;
	uint64_t sequence = 0;
	amdgpu_device_handle dev = NULL;
	uint32_t major = 0;
	uint32_t minor = 0;
	int ret;

	setenv("R5_GPU_DEV_DIR", "/tmp/r5-gpu-missing", 1);
	{
		drmDevicePtr slot = (drmDevicePtr)(uintptr_t)0x5a5a5a5a;
		int missing = r5_drm_get_devices2(0, &slot, 1);
		CHECK(missing == 0);
		CHECK(slot == (drmDevicePtr)(uintptr_t)0x5a5a5a5a);
	}
#if defined(R5_HOST_NEGATIVE_ERRNO)
	{
		drmDevicePtr none = NULL;
		int bad = r5_drm_get_device2(-1, 0, &none);
		CHECK(bad < 0);
		CHECK(bad == EINVAL);
		mkdir("/tmp/r5-gpu-empty", 0755);
		int outside = open("/tmp/r5-gpu-outside", O_RDWR | O_CREAT, 0644);
		CHECK(outside >= 0);
		setenv("R5_GPU_DEV_DIR", "/tmp/r5-gpu-empty", 1);
		bad = r5_drm_get_device2(outside, 0, &none);
		CHECK(bad < 0);
		CHECK(bad == ENODEV);
		close(outside);
		void *cpu = NULL;
		int area = -1;
		int mapped = r5_bo_cpu_map(3, 0, &cpu, &area);
		CHECK(mapped < 0);
		CHECK(mapped == ENOENT);
		mapped = r5_bo_cpu_unmap(3, 0);
		CHECK(mapped < 0);
		CHECK(mapped == ENOENT);
		gMapFault = 1;
		mapped = r5_bo_cpu_map(3, 1, &cpu, &area);
		CHECK(mapped < 0);
		CHECK(mapped == ENODEV);
		gMapFault = 2;
		mapped = r5_bo_cpu_map(3, 1, &cpu, &area);
		CHECK(mapped < 0);
		CHECK(mapped == ENOMEM);
		gMapFault = 0;
	}
#endif
	mkdir(dir, 0755);
	node = open(path, O_RDWR | O_CREAT, 0644);
	CHECK(node >= 0);
	setenv("R5_GPU_DEV_DIR", dir, 1);
	count = r5_drm_get_devices2(0, NULL, 0);
	CHECK(count == 1);
	count = r5_drm_get_devices2(0, listed, 4);
	CHECK(count == 1);
	CHECK(listed[0]->bustype == DRM_BUS_PCI);
	CHECK(listed[0]->available_nodes == (1 << DRM_NODE_RENDER));
	CHECK(strcmp(listed[0]->nodes[DRM_NODE_RENDER], path) == 0);
	CHECK(listed[0]->deviceinfo.pci->vendor_id == 0x1002);
	CHECK(listed[0]->deviceinfo.pci->device_id == 0x15e7);
	CHECK(listed[0]->businfo.pci->bus == 5);
	ret = r5_drm_get_device2(node, 0, &one);
	CHECK(ret == 0);
	CHECK(one != NULL);
	CHECK(strcmp(one->nodes[DRM_NODE_RENDER], path) == 0);
	drmFreeDevice(&listed[0]);
	drmFreeDevice(&one);

	memset(name, 0xa5, sizeof(name));
	memset(&raw, 0, sizeof(raw));
	raw.name = name;
	raw.name_len = sizeof(name);
	raw.date = date;
	raw.date_len = sizeof(date);
	raw.desc = desc;
	raw.desc_len = sizeof(desc);
	CHECK(drmIoctl(node, DRM_IOCTL_VERSION, &raw) == 0);
	CHECK(raw.version_major == 3);
	CHECK(raw.version_minor == 42);
	CHECK(raw.name_len == strlen("amdgpu"));
	CHECK(memcmp(name, "amdgpu", 6) == 0);
	CHECK((unsigned char)name[6] == 0xa5);

	version = drmGetVersion(node);
	CHECK(version != NULL);
	CHECK(version->version_minor >= 42);
	CHECK(strcmp(version->name, "amdgpu") == 0);
	drmFreeVersion(version);

	memset(&memory, 0, sizeof(memory));
	CHECK(info_query(node, AMDGPU_INFO_MEMORY, 0, &memory, sizeof(memory)) == 0);
	CHECK(memory.vram.total_heap_size == (256ull << 20));
	CHECK(memory.vram.max_allocation == (64ull << 20));
	CHECK(memory.gtt.max_allocation <= (64ull << 20));
	memset(&heap, 0, sizeof(heap));
	CHECK(info_query(node, AMDGPU_INFO_VRAM_GTT, 0, &heap, sizeof(heap)) == 0);
	{
		uint64_t usage = 0;
		uint32_t maxibs[AMDGPU_HW_IP_NUM];
		memset(maxibs, 0, sizeof(maxibs));
		CHECK(info_query(node, AMDGPU_INFO_VRAM_USAGE, 0, &usage,
			sizeof(usage)) == 0);
		CHECK(usage == 0x3000);
		CHECK(info_query(node, AMDGPU_INFO_VIS_VRAM_USAGE, 0, &usage,
			sizeof(usage)) == 0);
		CHECK(usage == 0x3000);
		CHECK(info_query(node, AMDGPU_INFO_GTT_USAGE, 0, &usage,
			sizeof(usage)) == 0);
		CHECK(usage == 0x4000);
		CHECK(info_query(node, AMDGPU_INFO_MAX_IBS, 0, maxibs,
			sizeof(maxibs)) == 0);
		CHECK(maxibs[AMDGPU_HW_IP_GFX] == 8);
		CHECK(maxibs[AMDGPU_HW_IP_COMPUTE] == 0);
		CHECK(maxibs[2] == 0);
		struct drm_amdgpu_info_video_caps caps;
		memset(&caps, 0xa5, sizeof(caps));
		CHECK(info_query(node, AMDGPU_INFO_VIDEO_CAPS,
			AMDGPU_INFO_VIDEO_CAPS_DECODE, &caps, sizeof(caps)) == 0);
		CHECK(caps.codec_info[0].valid == 0);
		CHECK(caps.codec_info[AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1].max_width == 0);
		memset(&caps, 0xa5, sizeof(caps));
		CHECK(info_query(node, AMDGPU_INFO_VIDEO_CAPS,
			AMDGPU_INFO_VIDEO_CAPS_ENCODE, &caps, sizeof(caps)) == 0);
		CHECK(caps.codec_info[AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC].valid == 0);
		CHECK(info_query(node, AMDGPU_INFO_VIDEO_CAPS, 9, &caps,
			sizeof(caps)) != 0);
	}
	CHECK(heap.gtt_size == (256ull << 20));
	memset(&ip, 0, sizeof(ip));
	CHECK(info_query(node, AMDGPU_INFO_HW_IP_INFO, AMDGPU_HW_IP_COMPUTE, &ip,
		sizeof(ip)) == 0);
	CHECK(ip.available_rings == 0);
	CHECK(info_query(node, AMDGPU_INFO_READ_MMR_REG, 0x263e, &mmr, sizeof(mmr)) == 0);
	CHECK(mmr == 0x24000042u);

	memset(&cap, 0, sizeof(cap));
	cap.capability = DRM_CAP_SYNCOBJ;
	CHECK(drmIoctl(node, DRM_IOCTL_GET_CAP, &cap) == 0);
	CHECK(cap.value == 1);
	cap.capability = DRM_CAP_SYNCOBJ_TIMELINE;
	CHECK(drmIoctl(node, DRM_IOCTL_GET_CAP, &cap) == 0);
	CHECK(cap.value == 1);
	cap.capability = DRM_CAP_DUMB_BUFFER;
	CHECK(drmIoctl(node, DRM_IOCTL_GET_CAP, &cap) == 0);
	CHECK(cap.value == 0);

	memset(&create, 0, sizeof(create));
	create.flags = DRM_SYNCOBJ_CREATE_SIGNALED;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &create) == 0);
	handles[0] = create.handle;
	points[0] = 0;
	memset(&timeline, 0, sizeof(timeline));
	timeline.handles = (uint64_t)(uintptr_t)handles;
	timeline.points = (uint64_t)(uintptr_t)points;
	timeline.count_handles = 1;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_QUERY, &timeline) == 0);
	CHECK(points[0] == 1);
	memset(&list, 0, sizeof(list));
	list.handles = (uint64_t)(uintptr_t)handles;
	list.count_handles = 1;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_RESET, &list) == 0);
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_QUERY, &timeline) == 0);
	CHECK(points[0] == 0);
	{
		struct drm_syncobj_wait binaryWait;
		memset(&binaryWait, 0, sizeof(binaryWait));
		binaryWait.handles = (uint64_t)(uintptr_t)handles;
		binaryWait.count_handles = 1;
		binaryWait.timeout_nsec = 0;
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &binaryWait) != 0);
		CHECK(errno == EINVAL);
	}
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_SIGNAL, &list) == 0);
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_QUERY, &timeline) == 0);
	CHECK(points[0] == (gSequence == 0 ? 1 : gSequence));

	memset(&create, 0, sizeof(create));
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_CREATE, &create) == 0);
	handles[1] = create.handle;
	{
		uint32_t both[2];
		struct drm_syncobj_wait anyWait;
		both[0] = handles[1];
		both[1] = handles[0];
		memset(&anyWait, 0, sizeof(anyWait));
		anyWait.handles = (uint64_t)(uintptr_t)both;
		anyWait.count_handles = 2;
		anyWait.timeout_nsec = 0;
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &anyWait) != 0);
		CHECK(errno == EINVAL);
		both[0] = handles[0];
		both[1] = handles[1];
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &anyWait) != 0);
		CHECK(errno == EINVAL);
		both[0] = handles[1];
		both[1] = handles[0];
		anyWait.flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &anyWait) == 0);
		CHECK(anyWait.first_signaled == 1);
		anyWait.flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT
			| DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_WAIT, &anyWait) != 0);
		CHECK(errno == ETIME);
	}
	ret = amdgpu_device_initialize(node, &major, &minor, &dev);
	CHECK(ret == 0);
	CHECK(minor >= 42);
	amdgpu_context_handle context = NULL;
	CHECK(amdgpu_cs_ctx_create(dev, &context) == 0);
	memset(&ib, 0, sizeof(ib));
	memset(sems, 0, sizeof(sems));
	memset(chunks, 0, sizeof(chunks));
	ib.va_start = 0x100000000ull;
	ib.ib_bytes = 32;
	ib.ip_type = AMDGPU_HW_IP_GFX;
	sems[0].handle = handles[0];
	sems[1].handle = handles[1];
	chunks[0].chunk_id = AMDGPU_CHUNK_ID_IB;
	chunks[0].length_dw = sizeof(ib) / 4;
	chunks[0].chunk_data = (uint64_t)(uintptr_t)&ib;
	chunks[1].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_IN;
	chunks[1].length_dw = sizeof(sems[0]) / 4;
	chunks[1].chunk_data = (uint64_t)(uintptr_t)&sems[0];
	chunks[2].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_OUT;
	chunks[2].length_dw = sizeof(sems[1]) / 4;
	chunks[2].chunk_data = (uint64_t)(uintptr_t)&sems[1];
	CHECK(amdgpu_cs_submit_raw2(dev, context, 0, 3, chunks, &sequence) == 0);
	CHECK(gCsContext == 1);
	points[1] = 0;
	timeline.count_handles = 1;
	timeline.handles = (uint64_t)(uintptr_t)&handles[1];
	timeline.points = (uint64_t)(uintptr_t)&points[1];
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_QUERY, &timeline) == 0);
	CHECK(points[1] == sequence);

	memset(syncs, 0, sizeof(syncs));
	syncs[0].handle = handles[1];
	syncs[0].point = sequence;
	syncs[1].handle = handles[0];
	syncs[1].point = 100;
	chunks[1].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT;
	chunks[1].length_dw = sizeof(syncs[0]) / 4;
	chunks[1].chunk_data = (uint64_t)(uintptr_t)&syncs[0];
	chunks[2].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL;
	chunks[2].length_dw = sizeof(syncs[1]) / 4;
	chunks[2].chunk_data = (uint64_t)(uintptr_t)&syncs[1];
	memset(&dep, 0, sizeof(dep));
	dep.ip_type = AMDGPU_HW_IP_GFX;
	dep.handle = sequence;
	chunks[3].chunk_id = AMDGPU_CHUNK_ID_DEPENDENCIES;
	chunks[3].length_dw = sizeof(dep) / 4;
	chunks[3].chunk_data = (uint64_t)(uintptr_t)&dep;
	CHECK(amdgpu_cs_submit_raw2(dev, context, 0, 4, chunks, &sequence) == 0);
	points[0] = 0;
	timeline.handles = (uint64_t)(uintptr_t)handles;
	timeline.points = (uint64_t)(uintptr_t)points;
	CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_QUERY, &timeline) == 0);
	CHECK(points[0] == 100);

	memset(&wait, 0, sizeof(wait));
	wait.in.handle = sequence;
	wait.in.timeout = 1000000000ull;
	wait.in.ip_type = AMDGPU_HW_IP_GFX;
	wait.in.ctx_id = 1;
	CHECK(drmCommandWriteRead(node, DRM_AMDGPU_WAIT_CS, &wait, sizeof(wait)) == 0);
	CHECK(wait.out.status == 0);
	wait.in.handle = sequence + 10;
	CHECK(drmCommandWriteRead(node, DRM_AMDGPU_WAIT_CS, &wait, sizeof(wait)) == 0);
	CHECK(wait.out.status == 1);

	ib.ip_type = AMDGPU_HW_IP_COMPUTE;
	chunks[0].chunk_data = (uint64_t)(uintptr_t)&ib;
	CHECK(amdgpu_cs_submit_raw2(dev, context, 0, 1, chunks, &sequence) != 0);
	ib.ip_type = AMDGPU_HW_IP_GFX;
	ib.flags = AMDGPU_IB_FLAG_PREEMPT;
	CHECK(amdgpu_cs_submit_raw2(dev, context, 0, 1, chunks, &sequence) != 0);

	{
		struct drm_syncobj_handle moved;
		memset(&moved, 0, sizeof(moved));
		moved.handle = handles[0];
		CHECK(drmIoctl(node, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &moved) != 0);
		CHECK(errno == ENOSYS);
		struct drm_prime_handle prime;
		memset(&prime, 0, sizeof(prime));
		CHECK(drmIoctl(node, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) != 0);
		CHECK(errno == ENOSYS);
	}
	submit_zero(node);
	binary_replay(node);

	CHECK(((1ull << 51) & 1ull) == 0);
	{
		struct drm_amdgpu_gem_va va;
		union drm_amdgpu_gem_create gem;
		memset(&va, 0, sizeof(va));
		va.operation = AMDGPU_VA_OP_MAP;
		va.va_address = 0xffff800000002000ull;
		va.map_size = 4096;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_GEM_VA, &va,
			sizeof(va)) != 0);
		CHECK(errno == EINVAL);
		va.flags = AMDGPU_VM_PAGE_PRT;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_GEM_VA, &va,
			sizeof(va)) == 0);
		CHECK(gPagePte == (1ull << 51));
		CHECK(gVa == 0xffff800000002000ull);
		va.flags = 0;
		va.va_address = 0x200000000ull;
		va.flags = AMDGPU_VM_PAGE_PRT;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_GEM_VA, &va,
			sizeof(va)) == 0);
		CHECK(gPagePte == (1ull << 51));
		memset(&gem, 0, sizeof(gem));
		gem.in.bo_size = 4096;
		gem.in.alignment = 4096;
		gem.in.domains = AMDGPU_GEM_DOMAIN_GTT;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_GEM_CREATE, &gem,
			sizeof(gem)) == 0);
		va.handle = gem.out.handle;
		va.operation = AMDGPU_VA_OP_REPLACE;
		va.flags = AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_GEM_VA, &va,
			sizeof(va)) == 0);
		CHECK(gPagePte == 1ull);
		va.handle = 0;
		va.operation = AMDGPU_VA_OP_CLEAR;
		va.flags = 0;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_GEM_VA, &va,
			sizeof(va)) == 0);
		CHECK(gPagePte == 0);
		va.handle = gem.out.handle;
		va.operation = AMDGPU_VA_OP_UNMAP;
		CHECK(drmCommandWriteRead(node, DRM_AMDGPU_GEM_VA, &va,
			sizeof(va)) != 0);
		CHECK(errno == EINVAL);
		CHECK(gPagePte == 0);
	}
	gJob.live = 0;
	gWatch = 1;
	gJobAgeUs = 0;
	gLost = 0;
	gDeferRetire = 1;
	ib.flags = 0;
	ib.ip_type = AMDGPU_HW_IP_GFX;
	chunks[0].chunk_id = AMDGPU_CHUNK_ID_IB;
	chunks[0].length_dw = sizeof(ib) / 4;
	chunks[0].chunk_data = (uint64_t)(uintptr_t)&ib;
	{
		uint64_t held = 0;
		uint32_t expired = 1;
		struct amdgpu_cs_fence query;
		uint32_t working = 0;
		CHECK(amdgpu_cs_submit_raw2(dev, context, 0, 1, chunks, &held) == 0);
		gDeferRetire = 0;
		memset(&query, 0, sizeof(query));
		query.context = context;
		query.ip_type = AMDGPU_HW_IP_GFX;
		query.fence = held;
		CHECK(amdgpu_cs_query_fence_status(&query, 1000, 0, &expired) == 0);
		CHECK(expired == 0);
		CHECK(gLost == 0);
		CHECK(info_query(node, AMDGPU_INFO_ACCEL_WORKING, 0, &working,
			sizeof(working)) == 0);
		gJobAgeUs = 6000000;
		CHECK(amdgpu_cs_submit_raw2(dev, context, 0, 1, chunks, &held) != 0);
		CHECK(errno == EIO);
		CHECK(gLost == 1);
	}
	gWatch = 0;
	gLost = 0;
	gJob.live = 0;
	gJobAgeUs = 0;

	printf("replay peaks bo %u sync %u\n", gBoPeak, gSyncPeak);
	close(node);
	return gFail;
}


int
main(void)
{
	int fd = open("/dev/null", O_RDWR);
	uint32_t major = 0;
	uint32_t minor = 0;
	amdgpu_device_handle dev = NULL;
	int ret = amdgpu_device_initialize(fd, &major, &minor, &dev);
	CHECK(ret == 0);
	CHECK(major == 3);
	CHECK(dev != NULL);
	CHECK(gReadOffset == 0x063eu);

	uint32_t working = 0;
	ret = amdgpu_query_info(dev, AMDGPU_INFO_ACCEL_WORKING, sizeof(working),
		&working);
	CHECK(ret == 0);
	CHECK(working == 1);

	struct drm_amdgpu_info_device device;
	memset(&device, 0, sizeof(device));
	ret = amdgpu_query_info(dev, AMDGPU_INFO_DEV_INFO, sizeof(device), &device);
	CHECK(ret == 0);
	CHECK(device.family == R5_AMDGPU_FAMILY_RV);
	CHECK(device.high_va_offset == R5_GPU_HIGH_VA_OFFSET);
	CHECK(device.high_va_max == R5_GPU_HIGH_VA_MAX);
	CHECK(device.cu_active_number == 7);

	uint32_t fwVer = 0;
	uint32_t fwFeature = 0;
	ret = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_GFX_ME, 0, 0,
		&fwVer, &fwFeature);
	CHECK(ret == 0);
	CHECK(fwVer == 0x11);
	CHECK(fwFeature == 0x22);

	struct drm_amdgpu_info_hw_ip ip;
	memset(&ip, 0, sizeof(ip));
	ret = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_GFX, 0, &ip);
	CHECK(ret == 0);
	CHECK(ip.hw_ip_version_major == 9);
	CHECK(ip.hw_ip_version_minor == 3);
	CHECK(ip.available_rings == 1);

	uint64_t radvVa = 0;
	amdgpu_va_handle radvRange = NULL;
	ret = amdgpu_va_range_alloc(dev, amdgpu_gpu_va_range_general, 4096, 4096,
		0, &radvVa, &radvRange, AMDGPU_VA_RANGE_HIGH);
	if (ret != 0)
		printf("f2 fail amdgpu_va_range_alloc %d\n", ret);
	else {
		struct amdgpu_bo_alloc_request radvRequest;
		memset(&radvRequest, 0, sizeof(radvRequest));
		radvRequest.alloc_size = 4096;
		radvRequest.preferred_heap = AMDGPU_GEM_DOMAIN_VRAM;
		amdgpu_bo_handle radvBo = NULL;
		ret = amdgpu_bo_alloc(dev, &radvRequest, &radvBo);
		if (ret != 0)
			printf("f2 fail GEM_CREATE %d\n", ret);
		else {
			ret = amdgpu_bo_va_op(radvBo, 0, 4096, radvVa, 0, AMDGPU_VA_OP_MAP);
			if (ret != 0)
				printf("f2 fail GEM_VA MAP %d\n", ret);
			else
				printf("f2 device path: no refusal\n");
			amdgpu_bo_va_op(radvBo, 0, 4096, radvVa, 0, AMDGPU_VA_OP_UNMAP);
			amdgpu_bo_free(radvBo);
		}
		amdgpu_va_range_free(radvRange);
	}
	{
		struct amdgpu_bo_alloc_request huge;
		memset(&huge, 0, sizeof(huge));
		huge.alloc_size = HOST_MAX_BO + 4096;
		huge.preferred_heap = AMDGPU_GEM_DOMAIN_VRAM;
		amdgpu_bo_handle hugeBo = NULL;
		ret = amdgpu_bo_alloc(dev, &huge, &hugeBo);
		printf("f2 oversize GEM_CREATE %d\n", ret);
		if (hugeBo != NULL)
			amdgpu_bo_free(hugeBo);
	}
	{
		struct drm_amdgpu_gem_va replace;
		memset(&replace, 0, sizeof(replace));
		replace.operation = AMDGPU_VA_OP_REPLACE;
		replace.handle = 1;
		replace.va_address = R5_GPU_HIGH_VA_OFFSET;
		replace.map_size = 4096;
		ret = drmIoctl(fd, DRM_IOCTL_AMDGPU_GEM_VA, &replace);
		printf("f2 direct GEM_VA REPLACE ret %d errno %d\n", ret, errno);
	}
	printf("f2 vkCreateDevice: not run\n");

	struct amdgpu_bo_alloc_request request;
	memset(&request, 0, sizeof(request));
	request.alloc_size = 8192;
	request.preferred_heap = AMDGPU_GEM_DOMAIN_GTT;
	amdgpu_bo_handle bo = NULL;
	ret = amdgpu_bo_alloc(dev, &request, &bo);
	CHECK(ret == 0);
	CHECK(gBoDomain == R5_GPU_DOMAIN_GTT);
	void *cpu = NULL;
	ret = amdgpu_bo_cpu_map(bo, &cpu);
	CHECK(ret == 0);
	CHECK(cpu != NULL);
	memset(cpu, 0x5a, 16);
	CHECK(((unsigned char *)cpu)[0] == 0x5a);
	ret = amdgpu_bo_cpu_unmap(bo);
	CHECK(ret == 0);
	CHECK(gClones == 1);
	CHECK(gCloneFreed == 0);
	{
		void *again = NULL;
		ret = amdgpu_bo_cpu_map(bo, &again);
		CHECK(ret == 0);
		CHECK(again == cpu);
		CHECK(gClones == 1);
		amdgpu_bo_cpu_unmap(bo);
	}
	{
		int saved = gCurrentTeam;
		r5_gpu_bo other;
		uint32_t mapped = 0;
		uint32_t i;
		for (i = 0; i < HOST_BO_SLOTS; i++) {
			if (gBoArea[i] > 0)
				mapped = i + 1;
		}
		gCurrentTeam = 2;
		memset(&other, 0, sizeof(other));
		other.handle = mapped;
		CHECK(mapped != 0);
		CHECK(r5_host_ioctl(fd, R5_GPU_BO_MAP, &other, sizeof(other)) != 0);
		gCurrentTeam = saved;
	}
	CHECK(host_cover(1, 4096, 1) == 0);
	CHECK(host_cover(0, 4096, 1) == 1);
	CHECK(host_cover(0, 257u * 4096u, 257) == 257);
	{
		amdgpu_bo_handle kernelBo = NULL;
		struct amdgpu_bo_alloc_request kernelRequest;
		void *kernelCpu = NULL;
		memset(&kernelRequest, 0, sizeof(kernelRequest));
		kernelRequest.alloc_size = 4096;
		kernelRequest.preferred_heap = AMDGPU_GEM_DOMAIN_GTT;
		gForceKernelClone = 1;
		ret = amdgpu_bo_alloc(dev, &kernelRequest, &kernelBo);
		CHECK(ret == 0);
		ret = amdgpu_bo_cpu_map(kernelBo, &kernelCpu);
		CHECK(ret != 0);
		CHECK(kernelCpu == NULL);
		gForceKernelClone = 0;
		amdgpu_bo_free(kernelBo);
	}
	{
		uint64_t gttHigh = 0;
		amdgpu_va_handle gttRange = NULL;
		ret = amdgpu_va_range_alloc(dev, amdgpu_gpu_va_range_general, 4096,
			4096, 0, &gttHigh, &gttRange, AMDGPU_VA_RANGE_HIGH);
		CHECK(ret == 0);
		CHECK(gttHigh >= R5_GPU_HIGH_VA_OFFSET);
		CHECK(gttHigh < R5_GPU_HIGH_VA_MAX);
		ret = amdgpu_bo_va_op(bo, 0, 4096, gttHigh, 0, AMDGPU_VA_OP_MAP);
		CHECK(ret == 0);
		amdgpu_va_range_free(gttRange);
	}

	uint64_t low = 0;
	uint64_t high = 0;
	amdgpu_va_handle lowVa = NULL;
	amdgpu_va_handle highVa = NULL;
	ret = amdgpu_va_range_alloc(dev, amdgpu_gpu_va_range_general, 4096, 4096,
		0, &low, &lowVa, 0);
	CHECK(ret == 0);
	CHECK(low >= 0x100000000ull);
	CHECK(low < R5_GPU_VA_MAX);
	ret = amdgpu_va_range_alloc(dev, amdgpu_gpu_va_range_general, 4096, 4096,
		0, &high, &highVa, AMDGPU_VA_RANGE_HIGH | AMDGPU_VA_RANGE_32_BIT);
	CHECK(ret == 0);
	CHECK(high >= R5_GPU_HIGH_VA_OFFSET);
	CHECK(high < R5_GPU_HIGH_VA_OFFSET + 0x100000000ull);

	ret = amdgpu_bo_va_op_raw(dev, bo, 0x1000, 0x1000, low,
		AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE, AMDGPU_VA_OP_MAP);
	CHECK(ret == 0);
	CHECK(gBoOffset == 0x1000);
	CHECK(gMapBytes == 0x1000);
	CHECK(gVa == low);

	amdgpu_context_handle context = NULL;
	ret = amdgpu_cs_ctx_create(dev, &context);
	CHECK(ret == 0);
	struct drm_amdgpu_cs_chunk_ib ib;
	struct drm_amdgpu_cs_chunk_syncobj wait;
	struct drm_amdgpu_cs_chunk_syncobj signal;
	struct drm_amdgpu_cs_chunk chunks[3];
	memset(&ib, 0, sizeof(ib));
	memset(&wait, 0, sizeof(wait));
	memset(&signal, 0, sizeof(signal));
	memset(chunks, 0, sizeof(chunks));
	ib.va_start = low;
	ib.ib_bytes = 32;
	ib.ip_type = AMDGPU_HW_IP_GFX;
	wait.handle = 4;
	wait.point = 9;
	signal.handle = 5;
	signal.point = 10;
	chunks[0].chunk_id = AMDGPU_CHUNK_ID_IB;
	chunks[0].length_dw = sizeof(ib) / 4;
	chunks[0].chunk_data = (uint64_t)(uintptr_t)&ib;
	chunks[1].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT;
	chunks[1].length_dw = sizeof(wait) / 4;
	chunks[1].chunk_data = (uint64_t)(uintptr_t)&wait;
	chunks[2].chunk_id = AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL;
	chunks[2].length_dw = sizeof(signal) / 4;
	chunks[2].chunk_data = (uint64_t)(uintptr_t)&signal;
	uint64_t sequence = 0;
	ret = amdgpu_cs_submit_raw2(dev, context, 0, 3, chunks, &sequence);
	CHECK(ret == 0);
	CHECK(sequence == 11);
	CHECK(gIbCount == 1);
	CHECK(gIbVa == low);
	CHECK(gIbDwords == 8);
	CHECK(gWaitCount == 1);
	CHECK(gWaitHandle == 4);
	CHECK(gWaitPoint == 9);
	CHECK(gSignalCount == 1);
	CHECK(gCalls > 0);
	CHECK(gShort == 0);
	radv_replay(fd);

	if (ac_drm_replay(fd) != 0)
		gFail++;

	if (gFail == 0)
		printf("ok libdrm ioctl\n");
	return gFail == 0 ? 0 : 1;
}
