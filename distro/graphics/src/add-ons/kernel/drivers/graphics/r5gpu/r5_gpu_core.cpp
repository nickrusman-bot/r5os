/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * GPU core: clients, buffers, sync objects, jobs, the per-file VMID
 * book, limits, user-memory copies, and the watchdog. Hardware goes
 * through r5_gpu_backend_ops. A null operation returns B_NOT_SUPPORTED.
 *
 * Render node /dev/gpu/radeon_hd/<n>. The graphics node is unchanged.
 * Without gpu_render the node only answers GET_INFO. VMID 1 is one
 * shared address space unless gfx9_vmid_per_file is set. A job that
 * stays in the ring for 5 seconds
 * marks the device lost. A client wait does not. There is no reset.
 * The GFX watchdog is 10 seconds, the same as amdgpu.lockup_timeout.
 */

#include "r5_gpu_internal.h"
#include "r5_cs.h"
#include "r5_defer.h"
#include "r5_log.h"
#include "r5_span.h"
#include "r5_vmid.h"
#include "r5_wait.h"

#include <KernelExport.h>
#include <kernel.h>
#include <Drivers.h>

#include <driver_settings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


extern "C" status_t vm_delete_area(team_id teamID, area_id areaID, bool kernel);
extern "C" bool team_is_valid(team_id id);
extern "C" status_t vm_set_area_page_memory_type(area_id id, uint32 type);

static void r5_gpu_eop();
static void gpu_fault_retire();
static status_t gpu_wait_value(void* info, volatile uint64* value, uint64 target,
	uint64 timeoutUs);
static status_t gpu_bo_map(gpu_client* client, void* buffer, size_t length);
static status_t gpu_free(void* cookie);
static status_t gpu_read(void* cookie, off_t position, void* buffer, size_t* numBytes);
static status_t gpu_write(void* cookie, off_t position, const void* buffer,
	size_t* numBytes);


int sErrorLines;
int sOnceBits;
int sDebugOn = -1;
int sSelftestOn = -1;
int sDeferred = 0;
static r5_defer sDefer[256];

gpu_bo sBo[kMaxBo];
gpu_job sJobs[kMaxJob];
static gpu_prt sPrt[kMaxPrt];
uint64 sHighestSequence;
uint64 sLastIbVa;
uint64 sLastIbList[8];
uint32 sLastIbDw[8];
uint32 sLastIbN;
static r5_vmid_mgr sFileVmids;
static bool sFileVmidsReady = false;
static uint32 sNextFileOwner = 1;
static bool sReplacing;
lock sGpuLock;
ConditionVariable sEop;
bool sReady = false;
bool sLost = false;
gpu_client* sGuilty = NULL;
int32 sContexts = 0;
static r5_ctx_counter sCtxCounter;
r5_pasid_ida sPasids;
gpu_client* sClientList;
void* sInfo;
static sem_id sRetireSem = -1;
static thread_id sRetireThread = -1;


bool gpu_selftest_enabled();
static bool gpu_job_blocks(const gpu_sync* sync, uint64 target, bool binary);
static void gpu_sync_host(gpu_sync* sync);
static void gpu_release_parked(uint64 seen);
static void gpu_watchdog();
static status_t gpu_file_root(gpu_client* client);
static void gpu_defer_reclaim(uint64 seen);
static void gpu_prt_release_tree(r5_vm* vm);



static bool
gpu_hw_map(r5_vm* vm, uint64 va, uint64 pte)
{
	if (gR5GpuOps.vm_map == NULL)
		return false;
	return gR5GpuOps.vm_map(vm, va, pte) != 0;
}


static bool
gpu_hw_lookup(r5_vm* vm, uint64 va)
{
	if (gR5GpuOps.vm_lookup == NULL)
		return false;
	return gR5GpuOps.vm_lookup(vm, va) != 0;
}


static bool
gpu_hw_prt(r5_vm* vm, uint64 va)
{
	if (gR5GpuOps.map_prt == NULL)
		return false;
	return gR5GpuOps.map_prt(vm, va) != 0;
}


static bool
gpu_hw_flush()
{
	if (gR5GpuOps.vm_flush == NULL)
		return false;
	return gR5GpuOps.vm_flush() != 0;
}


static bool
gpu_hw_prepare()
{
	if (gR5GpuOps.vmid_prepare == NULL)
		return false;
	return gR5GpuOps.vmid_prepare() != 0;
}


static int
gpu_hw_per_file()
{
	if (gR5GpuOps.vmid_per_file == NULL)
		return -1;
	return gR5GpuOps.vmid_per_file() ? 1 : 0;
}


static r5_vm*
gpu_hw_shared()
{
	if (gR5GpuOps.shared_vm == NULL)
		return NULL;
	return (r5_vm*)gR5GpuOps.shared_vm();
}


static uint64
gpu_hw_seen()
{
	if (gR5GpuOps.user_seen == NULL)
		return 0;
	return gR5GpuOps.user_seen();
}


static void
gpu_hw_vram_free(uint64 offset)
{
	if (gR5GpuOps.vram_free != NULL)
		gR5GpuOps.vram_free(offset);
}


static void
gpu_hw_dump(const char* why)
{
	if (gR5GpuOps.dump != NULL)
		gR5GpuOps.dump(why);
}


bool
gpu_debug_enabled()
{
	if (sDebugOn < 0)
		gpu_cache_log_settings();
	return sDebugOn != 0;
}


bool
gpu_dump_begin()
{
	return r5_log_allow(0, 0, R5_LOG_DUMP, &sErrorLines, &sOnceBits, -1) != 0;
}


bool
gpu_dump_line()
{
	return r5_log_allow(0, 0, R5_LOG_DUMP, &sErrorLines, &sOnceBits, 0) != 0;
}


static bool
gpu_error_line()
{
	return r5_log_allow(0, 0, R5_LOG_ERROR, &sErrorLines, &sOnceBits, 0) != 0;
}


bool
r5_gpu_log_error()
{
	return gpu_error_line();
}


/* The first 32 refusals in a boot. Later ones still return the status. */
status_t
gpu_refuse(const char* op, const char* why, status_t status, uint32 handle,
	uint32 domain, uint64 bytes, uint64 va, uint64 offset, uint64 mapBytes,
	uint32 flags)
{
	if (gpu_error_line()) {
		dprintf("radeon_hd: refuse %s %s status %#x handle %lu domain %lu "
			"bytes %llu va %#llx offset %llu map %llu flags %#lx\n",
			op, why, (unsigned)status, (unsigned long)handle,
			(unsigned long)domain, (unsigned long long)bytes,
			(unsigned long long)va, (unsigned long long)offset,
			(unsigned long long)mapBytes, (unsigned long)flags);
	}
	return status;
}


bool
gpu_render_enabled()
{
	bool enabled = false;
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		enabled = get_driver_boolean_parameter(settings, "gpu_render",
			false, true);
		unload_driver_settings(settings);
	}
	return enabled;
}


void
gpu_lock()
{
	acquire_lock(&sGpuLock);
}


void
gpu_unlock()
{
	release_lock(&sGpuLock);
}


void
gpu_lost(void* info, const char* why)
{
	if (sLost)
		return;
	sLost = true;
	(void)info;
	for (uint32 i = 0; i < kMaxJob; i++)
		sJobs[i].live = false;
	sEop.NotifyAll();
	gpu_hw_dump(why);
}


status_t
gpu_copy_in(void* buffer, size_t length, void* local, size_t bytes)
{
	if (buffer == NULL || bytes < 8 || length < bytes)
		return B_BAD_VALUE;
	if (!is_user_address_range(buffer, bytes))
		return B_BAD_ADDRESS;
	if (user_memcpy(local, buffer, bytes) != B_OK)
		return B_BAD_ADDRESS;
	uint32 size = *(uint32*)local;
	if (size != (uint32)bytes || size > length)
		return B_BAD_VALUE;
	return B_OK;
}


status_t
gpu_copy_out(void* buffer, size_t length, const void* local, size_t bytes)
{
	if (!is_user_address_range(buffer, bytes) || length < bytes)
		return B_BAD_ADDRESS;
	if (user_memcpy(buffer, local, bytes) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static status_t
gpu_copy_user(void* local, uint64 address, size_t bytes)
{
	if (bytes == 0)
		return B_OK;
	const void* user = (const void*)(addr_t)address;
	if (!is_user_address_range(user, bytes))
		return B_BAD_ADDRESS;
	if (user_memcpy(local, user, bytes) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static status_t
gpu_retire_thread(void* data)
{
	(void)data;
	while (sReady) {
		status_t got = acquire_sem_etc(sRetireSem, 1, B_RELATIVE_TIMEOUT,
			1000000);
		if (!sReady)
			break;
		if (got != B_OK && got != B_TIMED_OUT)
			break;
		gpu_lock();
		/* The one-second timeout is the check when no interrupt arrives.
		 * Retire from the user fence first, then look at what is left. */
		r5_gpu_eop();
		gpu_fault_retire();
		gpu_release_parked(gpu_hw_seen());
		gpu_hw_dump(NULL);
		gpu_watchdog();
		gpu_unlock();
	}
	return B_OK;
}


extern "C" void
r5_gpu_retire_wake()
{
	if (sRetireSem >= 0)
		release_sem_etc(sRetireSem, 1, B_DO_NOT_RESCHEDULE);
}


extern "C" void
r5_gpu_init()
{
	if (sReady)
		return;
	gpu_cache_log_settings();
	init_lock(&sGpuLock, "r5 gpu");
	sEop.Init(&sEop, "r5 gpu eop");
	sRetireSem = create_sem(0, "r5 gpu retire");
	sReady = true;
	if (sRetireSem >= 0) {
		sRetireThread = spawn_kernel_thread(gpu_retire_thread,
			"r5 gpu retire", B_NORMAL_PRIORITY, NULL);
		if (sRetireThread >= 0)
			resume_thread(sRetireThread);
	}
}


extern "C" void
r5_gpu_uninit()
{
	sReady = false;
	if (sRetireSem >= 0)
		release_sem(sRetireSem);
	if (sRetireThread >= 0) {
		status_t result = B_OK;
		wait_for_thread(sRetireThread, &result);
		sRetireThread = -1;
	}
	if (sRetireSem >= 0) {
		delete_sem(sRetireSem);
		sRetireSem = -1;
	}
}


static r5_vm*
gpu_vm(gpu_client* client)
{
	if (client != NULL && client->tree != NULL)
		return &client->tree->vm;
	return gpu_hw_shared();
}


static r5_file_tree*
gpu_tree_of(r5_vm* vm)
{
	if (vm == NULL || vm == gpu_hw_shared())
		return NULL;
	return (r5_file_tree*)vm;
}


static void
r5_file_tree_free(r5_file_tree* tree)
{
	if (tree->owner != 0)
		r5_vmid_release(&sFileVmids, tree->owner, tree->seen);
	gpu_prt_release_tree(&tree->vm);
	free(tree->pages);
	free(tree);
}


void
r5_file_tree_drop(r5_file_tree* tree)
{
	if (tree == NULL)
		return;
	if (r5_file_count_drop(&tree->count, (int32)tree->pagesN))
		r5_file_tree_free(tree);
}


void
gpu_bo_hold_tree(gpu_bo* bo)
{
	if (bo->hold != NULL)
		return;
	for (uint32 i = 0; i < kMaxRange; i++) {
		if (!bo->ranges[i].live)
			continue;
		r5_file_tree* tree = gpu_tree_of(bo->ranges[i].vm);
		if (tree == NULL)
			continue;
		r5_file_count_hold(&tree->count);
		bo->hold = tree;
		return;
	}
}


static void
gpu_bo_unmap(gpu_bo* bo)
{
	bool any = false;
	for (uint32 i = 0; i < kMaxRange; i++) {
		gpu_range* range = &bo->ranges[i];
		if (!range->live)
			continue;
		r5_vm* vm = range->vm != NULL ? range->vm : gpu_hw_shared();
		for (uint64 page = 0; page < range->bytes; page += 4096)
			gpu_hw_map(vm, range->va + page, 0);
		range->live = false;
		range->vm = NULL;
		any = true;
	}
	if (any)
		gpu_hw_flush();
}


void
gpu_bo_release(gpu_bo* bo)
{
	if (!bo->live)
		return;
	gpu_bo_unmap(bo);
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		if (!sDeferred || !r5_defer_add(sDefer, 256, bo->vramOffset))
			gpu_hw_vram_free(bo->vramOffset);
	}
	/* The user clone belongs to mapTeam. A team that has already ended
	 * took its areas with it. team_is_valid is the check; do not delete
	 * an id in a team that is gone.
	 */
	if (bo->area >= 0 && team_is_valid(bo->mapTeam))
		vm_delete_area(bo->mapTeam, bo->area, true);
	if (bo->kernelArea >= 0)
		delete_area(bo->kernelArea);
	bo->live = false;
	bo->owner = NULL;
	bo->parked = false;
	bo->parkSequence = 0;
	if (bo->hold != NULL) {
		r5_file_tree* tree = bo->hold;
		bo->hold = NULL;
		r5_file_tree_drop(tree);
	}
	bo->area = -1;
	bo->mapTeam = -1;
	bo->kernelArea = -1;
	bo->kernel = NULL;
}


static status_t
gpu_bo_create(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_CREATE", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.bytes == 0 || in.bytes > kMaxBoBytes)
		return gpu_refuse("BO_CREATE", "size", B_BAD_VALUE, 0, in.domain,
			in.bytes, 0, 0, 0, in.flags);
	if (in.domain != R5_GPU_DOMAIN_VRAM && in.domain != R5_GPU_DOMAIN_GTT)
		return gpu_refuse("BO_CREATE", "domain", B_BAD_VALUE, 0, in.domain,
			in.bytes, 0, 0, 0, in.flags);
	if (client->handleCount >= kMaxClientBo)
		return gpu_refuse("BO_CREATE", "handles", B_NO_MEMORY, 0, in.domain,
			in.bytes, 0, 0, 0, in.flags);
	uint64 bytes = (in.bytes + 4095ull) & ~4095ull;
	int slot = -1;
	for (uint32 i = 0; i < kMaxBo; i++) {
		if (!sBo[i].live) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0)
		return gpu_refuse("BO_CREATE", "slots", B_NO_MEMORY, 0, in.domain,
			bytes, 0, 0, 0, in.flags);
	gpu_bo* bo = &sBo[slot];
	memset(bo, 0, sizeof(*bo));
	bo->area = -1;
	bo->mapTeam = -1;
	bo->kernelArea = -1;
	bo->domain = in.domain;
	bo->flags = in.flags;
	bo->bytes = bytes;
	if (in.domain == R5_GPU_DOMAIN_VRAM) {
		uint64 offset = 0;
		if (client->tree != NULL) {
			if (gR5GpuOps.vram_bytes == NULL)
				return gpu_refuse("BO_CREATE", "vram", B_NOT_SUPPORTED, 0,
					in.domain, bytes, 0, 0, 0, in.flags);
			uint64 limit = 0;
			if (gR5GpuOps.vram_bytes(client->info, &limit) == B_OK) {
			uint64 held = 0;
			for (uint32 i = 0; i < kMaxBo; i++) {
				if (sBo[i].live && sBo[i].owner == client
					&& sBo[i].domain == R5_GPU_DOMAIN_VRAM)
					held += sBo[i].bytes;
			}
			if (!r5_vram_fits(limit, held, client->tree->vm.used, bytes))
				return gpu_refuse("BO_CREATE", "vram", B_NO_MEMORY, 0,
					in.domain, bytes, 0, 0, 0, in.flags);
			}
		}
		if (gR5GpuOps.vram_alloc == NULL)
			return gpu_refuse("BO_CREATE", "vram", B_NOT_SUPPORTED, 0, in.domain,
				bytes, 0, 0, 0, in.flags);
		if (gR5GpuOps.vram_alloc(bytes, &offset) != B_OK)
			return gpu_refuse("BO_CREATE", "vram", B_NO_MEMORY, 0, in.domain,
				bytes, 0, 0, 0, in.flags);
		bo->vramOffset = offset;
	} else {
		void* kernel = NULL;
		/* Not B_CLONEABLE_AREA. The user mapping is vm_clone_area below,
		 * with kernel true, so the source does not have to be cloneable.
		 * A cloneable id would let any program map this buffer.
		 */
		area_id area = create_area("r5 gpu gtt", &kernel,
			B_ANY_KERNEL_ADDRESS, (size_t)bytes, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (area < 0 || kernel == NULL)
			return gpu_refuse("BO_CREATE", "gtt", area < 0 ? area : B_NO_MEMORY,
				0, in.domain, bytes, 0, 0, 0, in.flags);
		bo->kernelArea = area;
		bo->kernel = kernel;
	}
	/* The reply carries the flags that were used. Write-combined GTT
	 * needs gfx9_wc_gtt. Otherwise the page stays cached and snooped. */
	{
		uint32 used = in.flags;
		if ((used & R5_GPU_BO_WC) != 0) {
			int allow = in.domain == R5_GPU_DOMAIN_GTT
				&& gR5GpuOps.gtt_wc != NULL && gR5GpuOps.gtt_wc() != 0;
			if (!allow || vm_set_area_page_memory_type(bo->kernelArea,
					B_WRITE_COMBINING_MEMORY) != B_OK)
				used &= ~R5_GPU_BO_WC;
		}
		if (gpu_selftest_enabled()) {
			dprintf("radeon_hd: bo domain %lu requested %08lx used %08lx "
				"bytes %llu snooped %d\n",
				(unsigned long)in.domain, (unsigned long)in.flags,
				(unsigned long)used, (unsigned long long)bytes,
				in.domain == R5_GPU_DOMAIN_GTT
					&& (used & R5_GPU_BO_WC) == 0 ? 1 : 0);
		}
		in.flags = used;
	}
	bo->live = true;
	bo->owner = client;
	client->handles[client->handleCount++] = (uint32)slot + 1u;
	in.handle = (uint32)slot + 1u;
	in.area = bo->area;
	in.bytes = bytes;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_CREATE", "copy", status, in.handle, in.domain,
			bytes, 0, 0, 0, in.flags);
	return B_OK;
}


gpu_bo*
gpu_bo_get(gpu_client* client, uint32 handle)
{
	if (handle == 0 || handle > kMaxBo)
		return NULL;
	gpu_bo* bo = &sBo[handle - 1u];
	if (!bo->live || bo->owner != client)
		return NULL;
	return bo;
}


void
gpu_defer_stamp(uint64 sequence)
{
	r5_defer_stamp(sDefer, 256, sequence);
}


static void
gpu_defer_reclaim(uint64 seen)
{
	uint64 page = 0;
	while (r5_defer_take(sDefer, 256, seen, &page)) {
		dprintf("radeon_hd: deferred page %llx reused\n",
			(unsigned long long)page);
		gpu_hw_vram_free(page);
	}
}


static void
gpu_release_parked(uint64 seen)
{
	/* A lost device never retires the fence, so every parked buffer
	 * and the file tree it holds are released. */
	if (sLost)
		seen = ~0ull;
	gpu_defer_reclaim(seen);
	for (uint32 i = 0; i < kMaxBo; i++) {
		gpu_bo* bo = &sBo[i];
		if (!bo->live || !bo->parked)
			continue;
		if (seen != ~0ull && bo->parkSequence > seen)
			continue;
		gpu_bo_release(bo);
	}
}


static void
gpu_watchdog()
{
	if (sLost || sInfo == NULL)
		return;
	bigtime_t now = system_time();
	uint64 seen = gpu_hw_seen();
	gpu_job* oldest = NULL;
	for (uint32 i = 0; i < kMaxJob; i++) {
		gpu_job* job = &sJobs[i];
		if (!job->live || job->sequence <= seen || job->submitted == 0)
			continue;
		if (oldest == NULL || job->submitted < oldest->submitted)
			oldest = job;
	}
	if (oldest == NULL || now - oldest->submitted < kWatchdogUs)
		return;
	if (sGuilty == NULL)
		sGuilty = oldest->owner;
	gpu_lost(sInfo, "gpu lost watchdog");
	gpu_release_parked(~0ull);
}


static void
gpu_bo_detach(gpu_client* client, gpu_bo* bo, uint32 handle)
{
	for (uint32 i = 0; i < client->handleCount; i++) {
		if (client->handles[i] != handle)
			continue;
		client->handles[i] = client->handles[client->handleCount - 1];
		client->handleCount--;
		break;
	}
	uint64 sequence = client->lastSequence;
	uint64 seen = gpu_hw_seen();
	if (!sLost && sequence != 0 && seen < sequence) {
		bo->owner = NULL;
		bo->parked = true;
		bo->parkSequence = sequence;
		gpu_bo_hold_tree(bo);
		return;
	}
	gpu_bo_release(bo);
}


static status_t
gpu_bo_free(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return B_BAD_VALUE;
	gpu_bo_detach(client, bo, in.handle);
	return B_OK;
}


static bool
gpu_va_fits(uint64 va, uint64 bytes)
{
	return r5_vm_va_fits(va, bytes) != 0;
}


static void
gpu_va_clear(r5_vm* vm, uint64 va, uint64 bytes)
{
	if (vm == NULL)
		vm = gpu_hw_shared();
	for (uint64 page = 0; page < bytes; page += 4096)
		gpu_hw_map(vm, va + page, 0);
	if (bytes != 0)
		gpu_hw_flush();
}


static bool
gpu_span_inside(uint64 innerVa, uint64 innerBytes, uint64 va, uint64 bytes)
{
	uint64 innerEnd = innerVa + innerBytes;
	uint64 end = va + bytes;
	return innerBytes != 0 && innerEnd >= innerVa && end >= va
		&& innerVa >= va && innerEnd <= end;
}


static bool
gpu_same_vm(r5_vm* rangeVm, r5_vm* vm)
{
	if (rangeVm == NULL)
		rangeVm = gpu_hw_shared();
	if (vm == NULL)
		vm = gpu_hw_shared();
	return rangeVm == vm;
}


static bool
gpu_bo_partial(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 b = 0; b < kMaxBo; b++) {
		if (!sBo[b].live)
			continue;
		for (uint32 i = 0; i < kMaxRange; i++) {
			gpu_range* range = &sBo[b].ranges[i];
			if (!range->live || !gpu_same_vm(range->vm, vm))
				continue;
			if (!r5_range_overlap(va, bytes, range->va, range->bytes))
				continue;
			if (!gpu_span_inside(range->va, range->bytes, va, bytes))
				return true;
		}
	}
	return false;
}


static bool
gpu_va_bo_overlap(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 b = 0; b < kMaxBo; b++) {
		if (!sBo[b].live)
			continue;
		for (uint32 i = 0; i < kMaxRange; i++) {
			gpu_range* range = &sBo[b].ranges[i];
			if (range->live && gpu_same_vm(range->vm, vm)
				&& r5_range_overlap(va, bytes, range->va, range->bytes))
				return true;
		}
	}
	return false;
}


static bool
gpu_prt_overlaps(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live && gpu_same_vm(sPrt[i].vm, vm)
			&& r5_range_overlap(va, bytes, sPrt[i].va, sPrt[i].bytes))
			return true;
	}
	return false;
}


static bool
gpu_prt_covers(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (!sPrt[i].live || !gpu_same_vm(sPrt[i].vm, vm))
			continue;
		if (gpu_span_inside(va, bytes, sPrt[i].va, sPrt[i].bytes))
			return true;
	}
	return false;
}


static void
gpu_ranges_drop_contained(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 b = 0; b < kMaxBo; b++) {
		for (uint32 i = 0; i < kMaxRange; i++) {
			gpu_range* range = &sBo[b].ranges[i];
			if (range->live && gpu_same_vm(range->vm, vm)
				&& gpu_span_inside(range->va, range->bytes, va, bytes))
				range->live = false;
		}
	}
}


static void
gpu_prt_drop_contained(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live && gpu_same_vm(sPrt[i].vm, vm)
			&& gpu_span_inside(sPrt[i].va, sPrt[i].bytes, va, bytes))
			sPrt[i].live = false;
	}
}


static void
gpu_prt_release_tree(r5_vm* vm)
{
	if (vm == NULL || vm == gpu_hw_shared())
		return;
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live && sPrt[i].vm == vm)
			sPrt[i].live = false;
	}
}


static status_t
gpu_prt_add(r5_vm* vm, uint64 va, uint64 bytes)
{
	if (gpu_prt_covers(vm, va, bytes))
		return B_OK;
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live)
			continue;
		sPrt[i].live = true;
		sPrt[i].va = va;
		sPrt[i].bytes = bytes;
		sPrt[i].vm = vm;
		return B_OK;
	}
	return B_NO_MEMORY;
}


static status_t
gpu_va_write_prt(r5_vm* vm, uint64 va, uint64 bytes)
{
	if (gR5GpuOps.map_prt == NULL)
		return B_NOT_SUPPORTED;
	/* The PTE is a value built here: bit 51, VALID clear, no address.
	 * r5_gfx9_vmid1_map writes that page-table entry. This path does
	 * not read or write a user address (SMAP).
	 */
	for (uint64 page = 0; page < bytes; page += 4096) {
		if (!gpu_hw_prt(vm, va + page)) {
			gpu_va_clear(vm, va, page);
			return B_NO_MEMORY;
		}
	}
	if (gR5GpuOps.vm_flush == NULL)
		return B_NOT_SUPPORTED;
	if (!gpu_hw_flush()) {
		gpu_va_clear(vm, va, bytes);
		return B_ERROR;
	}
	return B_OK;
}


static status_t
gpu_va_map_copied(gpu_client* client, const r5_gpu_va* copied)
{
	r5_gpu_va in = *copied;
	status_t status = B_OK;
	if (in.context != 1 || !gpu_va_fits(in.gpu_va, in.map_bytes))
		return gpu_refuse("VA_MAP", "fit", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (in.handle == 0) {
		if ((in.flags & R5_GPU_VA_PRT) == 0 || in.bo_offset != 0)
			return gpu_refuse("VA_MAP", "bo", B_BAD_VALUE, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		if (gpu_prt_overlaps(gpu_vm(client), in.gpu_va, in.map_bytes)
			|| gpu_va_bo_overlap(gpu_vm(client), in.gpu_va, in.map_bytes))
			return gpu_refuse("VA_MAP", "overlap", B_BAD_VALUE, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		if (gR5GpuOps.vmid_prepare == NULL)
			return gpu_refuse("VA_MAP", "vmid", B_NOT_SUPPORTED, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		if (!gpu_hw_prepare())
			return gpu_refuse("VA_MAP", "vmid", B_NO_INIT, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		for (uint64 page = 0; page < in.map_bytes; page += 4096) {
			if (gpu_hw_lookup(gpu_vm(client), in.gpu_va + page)) {
				gpu_va_clear(gpu_vm(client), in.gpu_va, page);
				return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE, in.handle, 0,
					0, in.gpu_va + page, in.bo_offset, in.map_bytes, in.flags);
			}
		}
		status = gpu_va_write_prt(gpu_vm(client), in.gpu_va, in.map_bytes);
		if (status != B_OK)
			return gpu_refuse("VA_MAP", "prt", status, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		status = gpu_prt_add(gpu_vm(client), in.gpu_va, in.map_bytes);
		if (status != B_OK) {
			gpu_va_clear(gpu_vm(client), in.gpu_va, in.map_bytes);
			return gpu_refuse("VA_MAP", "prt", status, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		}
		return B_OK;
	}
	if (!sReplacing && gpu_prt_overlaps(gpu_vm(client), in.gpu_va, in.map_bytes))
		return gpu_refuse("VA_MAP", "overlap", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return gpu_refuse("VA_MAP", "bo", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if ((in.bo_offset & 0xfffull) != 0 || in.bo_offset > bo->bytes
		|| in.map_bytes > bo->bytes - in.bo_offset)
		return gpu_refuse("VA_MAP", "offset", B_BAD_VALUE, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	for (uint32 i = 0; i < kMaxRange; i++) {
		gpu_range* range = &bo->ranges[i];
		if (!range->live)
			continue;
		if (r5_range_overlap(in.bo_offset, in.map_bytes, range->offset,
				range->bytes)
			|| r5_range_overlap(in.gpu_va, in.map_bytes, range->va,
				range->bytes))
			return gpu_refuse("VA_MAP", "overlap", B_BAD_VALUE, in.handle,
				bo->domain, bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
				in.flags);
	}
	int slot = -1;
	for (uint32 i = 0; i < kMaxRange; i++) {
		if (!bo->ranges[i].live) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0)
		return gpu_refuse("VA_MAP", "ranges", B_NO_MEMORY, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (gR5GpuOps.vmid_prepare == NULL)
		return gpu_refuse("VA_MAP", "vmid", B_NOT_SUPPORTED, in.handle,
			bo->domain, bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
			in.flags);
	if (!gpu_hw_prepare())
		return gpu_refuse("VA_MAP", "vmid", B_NO_INIT, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	uint32 flags = in.flags;
	if (flags == 0)
		flags = R5_GPU_VA_READ | R5_GPU_VA_WRITE;
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		if (gR5GpuOps.vram_phys == NULL || gR5GpuOps.encode_pte == NULL)
			return gpu_refuse("VA_MAP", "pte", B_NOT_SUPPORTED, in.handle,
				bo->domain, bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
				in.flags);
		uint64 phys = 0;
		if (gR5GpuOps.vram_phys(client->info,
				bo->vramOffset + in.bo_offset, &phys) != B_OK)
			return gpu_refuse("VA_MAP", "pte", B_NOT_SUPPORTED, in.handle,
				bo->domain, bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
				in.flags);
		uint64 pte = gR5GpuOps.encode_pte(phys, flags, 0);
		for (uint64 page = 0; page < in.map_bytes; page += 4096) {
			if (gpu_hw_lookup(gpu_vm(client), in.gpu_va + page)) {
				gpu_va_clear(gpu_vm(client), in.gpu_va, page);
				return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE, in.handle,
					bo->domain, bo->bytes, in.gpu_va + page, in.bo_offset,
					in.map_bytes, in.flags);
			}
			if (!gpu_hw_map(gpu_vm(client), in.gpu_va + page, pte)) {
				dprintf("radeon_hd: page table pool full\n");
				gpu_va_clear(gpu_vm(client), in.gpu_va, page);
				return gpu_refuse("VA_MAP", "pte", B_NO_MEMORY, in.handle,
					bo->domain, bo->bytes, in.gpu_va + page, in.bo_offset,
					in.map_bytes, in.flags);
			}
			pte += 4096;
		}
	} else {
		/* get_memory_map() returns B_OK and a size-0 end entry, so a
		 * count taken from that return is always 0. get_memory_map_etc
		 * writes the count. The address is the kernel area, never the
		 * user clone, so this does not touch user pages (SMAP). A kernel
		 * address selects the kernel address space inside that call.
		 * More than 256 physical pieces continues after B_BUFFER_OVERFLOW.
		 */
		if (bo->kernel == NULL)
			return gpu_refuse("VA_MAP", "gtt", B_NO_INIT, in.handle, bo->domain,
				bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		uint8* cursor = (uint8*)bo->kernel + in.bo_offset;
		uint64 filled = 0;
		while (filled < in.map_bytes) {
			physical_entry entries[256];
			uint32 count = 256;
			status_t mapped = get_memory_map_etc(B_CURRENT_TEAM, cursor,
				(size_t)(in.map_bytes - filled), entries, &count);
			if (mapped != B_OK && mapped != B_BUFFER_OVERFLOW) {
				gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
				return gpu_refuse("VA_MAP", "phys", mapped, in.handle,
					bo->domain, bo->bytes, in.gpu_va, in.bo_offset, filled,
					in.flags);
			}
			uint64 step = 0;
			for (uint32 entry = 0; entry < count && filled < in.map_bytes;
				entry++) {
				uint64 address = entries[entry].address;
				uint64 bytes = entries[entry].size;
				while (bytes >= 4096 && filled < in.map_bytes) {
					if (gpu_hw_lookup(gpu_vm(client), in.gpu_va + filled)) {
						gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
						return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE,
							in.handle, bo->domain, bo->bytes,
							in.gpu_va + filled, in.bo_offset, in.map_bytes,
							in.flags);
					}
					if (gR5GpuOps.encode_pte == NULL)
						return gpu_refuse("VA_MAP", "pte", B_NOT_SUPPORTED,
							in.handle, bo->domain, bo->bytes,
							in.gpu_va + filled, in.bo_offset, in.map_bytes,
							in.flags);
					uint64 pte = gR5GpuOps.encode_pte(address, flags, 1);
					if (!gpu_hw_map(gpu_vm(client), in.gpu_va + filled, pte)) {
						dprintf("radeon_hd: page table pool full\n");
						gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
						return gpu_refuse("VA_MAP", "pte", B_NO_MEMORY,
							in.handle, bo->domain, bo->bytes,
							in.gpu_va + filled, in.bo_offset, in.map_bytes,
							in.flags);
					}
					address += 4096;
					bytes -= 4096;
					filled += 4096;
					step += 4096;
				}
			}
			if (step == 0 || mapped == B_OK)
				break;
			cursor += step;
		}
		if (filled != in.map_bytes) {
			gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
			return gpu_refuse("VA_MAP", "short", B_BAD_DATA, in.handle,
				bo->domain, bo->bytes, in.gpu_va, in.bo_offset, filled,
				in.flags);
		}
	}
	if (gR5GpuOps.vm_flush == NULL || gR5GpuOps.vm_map == NULL)
		return gpu_refuse("VA_MAP", "flush", B_NOT_SUPPORTED, in.handle,
			bo->domain, bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
			in.flags);
	if (!gpu_hw_flush()) {
		gpu_va_clear(gpu_vm(client), in.gpu_va, in.map_bytes);
		return gpu_refuse("VA_MAP", "flush", B_ERROR, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	}
	bo->ranges[slot].live = true;
	bo->ranges[slot].va = in.gpu_va;
	bo->ranges[slot].offset = in.bo_offset;
	bo->ranges[slot].bytes = in.map_bytes;
	bo->ranges[slot].vm = gpu_vm(client);
	if (gpu_debug_enabled())
		dprintf("radeon_hd: t %lld va map %016llx bytes %llu domain %lu\n",
			(long long)system_time(),
			(unsigned long long)in.gpu_va,
			(unsigned long long)in.map_bytes,
			(unsigned long)bo->domain);
	return B_OK;
}


static status_t
gpu_va_map(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_va in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("VA_MAP", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	return gpu_va_map_copied(client, &in);
}


static status_t
gpu_va_replace(gpu_client* client, void* buffer, size_t length, bool clearOnly)
{
	const char* op = clearOnly ? "VA_CLEAR" : "VA_REPLACE";
	r5_gpu_va in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse(op, "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.context != 1 || !gpu_va_fits(in.gpu_va, in.map_bytes))
		return gpu_refuse(op, "fit", B_BAD_VALUE, in.handle, 0, 0, in.gpu_va,
			in.bo_offset, in.map_bytes, in.flags);
	if (!clearOnly && in.handle == 0 && (in.flags & R5_GPU_VA_PRT) == 0)
		return gpu_refuse(op, "bo", B_BAD_VALUE, in.handle, 0, 0, in.gpu_va,
			in.bo_offset, in.map_bytes, in.flags);
	if (!clearOnly && in.handle != 0 && gpu_bo_get(client, in.handle) == NULL)
		return gpu_refuse(op, "bo", B_BAD_VALUE, in.handle, 0, 0, in.gpu_va,
			in.bo_offset, in.map_bytes, in.flags);
	if (gpu_bo_partial(gpu_vm(client), in.gpu_va, in.map_bytes))
		return gpu_refuse(op, "overlap", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (gR5GpuOps.vmid_prepare == NULL || gR5GpuOps.vm_map == NULL
		|| gR5GpuOps.vm_flush == NULL)
		return gpu_refuse(op, "vmid", B_NOT_SUPPORTED, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (!gpu_hw_prepare())
		return gpu_refuse(op, "vmid", B_NO_INIT, in.handle, 0, 0, in.gpu_va,
			in.bo_offset, in.map_bytes, in.flags);
	gpu_va_clear(gpu_vm(client), in.gpu_va, in.map_bytes);
	gpu_ranges_drop_contained(gpu_vm(client), in.gpu_va, in.map_bytes);
	if (clearOnly) {
		gpu_prt_drop_contained(gpu_vm(client), in.gpu_va, in.map_bytes);
		return B_OK;
	}
	if (in.handle == 0) {
		status = gpu_va_write_prt(gpu_vm(client), in.gpu_va, in.map_bytes);
		if (status != B_OK)
			return gpu_refuse(op, "prt", status, in.handle, 0, 0, in.gpu_va,
				in.bo_offset, in.map_bytes, in.flags);
		status = gpu_prt_add(gpu_vm(client), in.gpu_va, in.map_bytes);
		if (status != B_OK) {
			gpu_va_clear(gpu_vm(client), in.gpu_va, in.map_bytes);
			return gpu_refuse(op, "prt", status, in.handle, 0, 0, in.gpu_va,
				in.bo_offset, in.map_bytes, in.flags);
		}
		return B_OK;
	}
	sReplacing = true;
	status = gpu_va_map_copied(client, &in);
	sReplacing = false;
	return status;
}


static status_t
gpu_va_unmap(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_va in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("VA_UNMAP", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL || in.context != 1)
		return gpu_refuse("VA_UNMAP", "bo", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (gR5GpuOps.vm_map == NULL || gR5GpuOps.vm_flush == NULL
		|| gR5GpuOps.shared_vm == NULL)
		return gpu_refuse("VA_UNMAP", "pte", B_NOT_SUPPORTED, in.handle,
			0, 0, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	for (uint32 i = 0; i < kMaxRange; i++) {
		gpu_range* range = &bo->ranges[i];
		if (!range->live || range->va != in.gpu_va
			|| range->offset != in.bo_offset || range->bytes != in.map_bytes)
			continue;
		r5_vm* vm = range->vm != NULL ? range->vm : gpu_hw_shared();
		for (uint64 page = 0; page < range->bytes; page += 4096) {
			if (!gpu_hw_map(vm, range->va + page, 0))
				return gpu_refuse("VA_UNMAP", "pte", B_BAD_ADDRESS, in.handle,
					bo->domain, bo->bytes, range->va + page, in.bo_offset,
					in.map_bytes, in.flags);
		}
		if (!gpu_hw_flush())
			return gpu_refuse("VA_UNMAP", "flush", B_ERROR, in.handle, bo->domain,
				bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		range->live = false;
		range->vm = NULL;
		return B_OK;
	}
	return gpu_refuse("VA_UNMAP", "range", B_BAD_VALUE, in.handle,
		bo != NULL ? bo->domain : 0, bo != NULL ? bo->bytes : 0, in.gpu_va,
		in.bo_offset, in.map_bytes, in.flags);
}


static bool
gpu_ib_mapped(gpu_client* client, uint64 va, uint32 dwords)
{
	r5_vm* vm = gpu_vm(client);
	uint64 first = 0;
	uint64 last = 0;
	if (!r5_ib_span(va, dwords, &first, &last))
		return false;
	for (uint64 page = first; ; page += 4096) {
		if (!gpu_hw_lookup(vm, page))
			return false;
		if (page == last)
			break;
	}
	return true;
}


/* The sync wait and the ring-room wait drop the lock. An unmap in that
 * window must fail the submit before the cache flush and the doorbell. */
static status_t
gpu_ibs_mapped(gpu_client* client, const r5_gpu_ib* ibs, uint32 count,
	uint32 ring)
{
	if (gR5GpuOps.vm_lookup == NULL)
		return B_NOT_SUPPORTED;
	for (uint32 i = 0; i < count; i++) {
		if (!gpu_ib_mapped(client, ibs[i].gpu_va, ibs[i].dwords))
			return gpu_refuse("CS_SUBMIT", "mapped", B_BAD_ADDRESS,
				1, 0, ibs[i].dwords, ibs[i].gpu_va, 0, ring, ibs[i].flags);
	}
	return B_OK;
}


static status_t
gpu_wait_room(void* info, uint32 dwords)
{
	(void)info;
	if (gR5GpuOps.user_room == NULL)
		return B_NOT_SUPPORTED;
	if (sLost)
		return ECANCELED;
	if (gR5GpuOps.user_room(dwords))
		return B_OK;
	bigtime_t deadline = system_time() + (bigtime_t)kRoomWaitUs;
	while (!gR5GpuOps.user_room(dwords)) {
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		bigtime_t now = system_time();
		if (gR5GpuOps.user_room(dwords))
			return B_OK;
		if (sLost)
			return ECANCELED;
		if (now >= deadline)
			return B_BUSY;
		gpu_unlock();
		entry.Wait(B_RELATIVE_TIMEOUT, deadline - now);
		gpu_lock();
	}
	return B_OK;
}


static bool
gpu_sync_signalled(uint64 value, uint64 submitted, uint64 target, bool binary)
{
	if (binary)
		return submitted > 0 && value >= submitted;
	return value >= target;
}


static bool
gpu_wait_ready(const gpu_sync* sync, const r5_gpu_cs_sync* wait)
{
	bool binary = (wait->flags & R5_GPU_CS_SYNC_BINARY) != 0;
	if (gpu_job_blocks(sync, wait->point, binary))
		return false;
	return gpu_sync_signalled(sync->point, sync->submitted, wait->point, binary);
}


static status_t
gpu_submit(gpu_client* client, void* buffer, size_t length)
{
	if (sLost)
		return gpu_refuse("CS_SUBMIT", "lost", ECANCELED, 0, 0, 0, 0, 0, 0, 0);
	r5_gpu_cs in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.context != 1 || in.ring != 0)
		return gpu_refuse("CS_SUBMIT", "context", B_BAD_VALUE, in.context, 0,
			in.ib_count, 0, 0, in.ring, 0);
	if (in.ib_count == 0 || in.ib_count > kMaxList
		|| in.wait_count > kMaxList || in.signal_count > kMaxList)
		return gpu_refuse("CS_SUBMIT", "count", B_BAD_VALUE, in.context, 0,
			in.ib_count, 0, 0, in.ring, 0);
	r5_gpu_ib ibs[kMaxList];
	r5_gpu_cs_sync waits[kMaxList];
	r5_gpu_cs_sync signals[kMaxList];
	status = gpu_copy_user(ibs, in.ibs, sizeof(r5_gpu_ib) * in.ib_count);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "ib", status, in.context, 0, in.ib_count,
			0, 0, in.ring, 0);
	status = gpu_copy_user(waits, in.waits, sizeof(r5_gpu_cs_sync) * in.wait_count);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "wait", status, in.context, 0, in.ib_count,
			0, 0, in.ring, 0);
	status = gpu_copy_user(signals, in.signals,
		sizeof(r5_gpu_cs_sync) * in.signal_count);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "signal", status, in.context, 0,
			in.ib_count, 0, 0, in.ring, 0);
	for (uint32 i = 0; i < in.ib_count; i++) {
		if (ibs[i].flags != 0)
			return gpu_refuse("CS_SUBMIT", "flags", B_BAD_VALUE, in.context, 0,
				in.ib_count, ibs[i].gpu_va, 0, in.ring, ibs[i].flags);
	}
	status = gpu_ibs_mapped(client, ibs, in.ib_count, in.ring);
	if (status != B_OK)
		return status;
	for (uint32 i = 0; i < in.wait_count; i++) {
		if (waits[i].handle == 0 || waits[i].handle > kMaxSync)
			return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
				in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
		gpu_sync* sync = &client->syncs[waits[i].handle - 1u];
		if (!sync->live)
			return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
				in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
		bigtime_t deadline = system_time() + (bigtime_t)kFreeWaitUs;
		while (!gpu_wait_ready(sync, &waits[i])) {
			if (sLost)
				return gpu_refuse("CS_SUBMIT", "lost", ECANCELED, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
			bigtime_t now = system_time();
			if (now >= deadline)
				return gpu_refuse("CS_SUBMIT", "wait", B_TIMED_OUT, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
			ConditionVariableEntry entry;
			sEop.Add(&entry);
			if (gpu_wait_ready(sync, &waits[i]))
				break;
			gpu_unlock();
			status_t waited = entry.Wait(
				B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT, deadline - now);
			gpu_lock();
			status = gpu_ibs_mapped(client, ibs, in.ib_count, in.ring);
			if (status != B_OK)
				return status;
			if (!sync->live)
				return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
			if (waited == B_INTERRUPTED || waited == B_WOULD_BLOCK)
				return gpu_refuse("CS_SUBMIT", "wait", waited, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
			if (waited == B_TIMED_OUT && !gpu_wait_ready(sync, &waits[i]))
				return gpu_refuse("CS_SUBMIT", "wait", B_TIMED_OUT, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
		}
	}
	int jobSlot = -1;
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (!sJobs[i].live) {
			jobSlot = (int)i;
			break;
		}
	}
	if (jobSlot < 0)
		return gpu_refuse("CS_SUBMIT", "job", B_BUSY, in.context, 0, in.ib_count,
			0, 0, in.ring, 0);
	if (gR5GpuOps.commit_dwords == NULL)
		return gpu_refuse("CS_SUBMIT", "room", B_NOT_SUPPORTED, in.context, 0,
			in.ib_count, ibs[0].gpu_va, 0, in.ring, 0);
	/* The file's page-table base is written after the room wait, so the
	 * count includes one VM flush that touch() will make due. */
	uint32 dwords = gR5GpuOps.commit_dwords(in.ib_count,
		client->tree != NULL ? 1 : 0, client->pasid);
	status = gpu_wait_room(client->info, dwords);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "room", status, in.context, 0, in.ib_count,
			ibs[0].gpu_va, 0, in.ring, 0);
	status = gpu_ibs_mapped(client, ibs, in.ib_count, in.ring);
	if (status != B_OK)
		return status;
	/* Temporary. A GTT indirect buffer is MTYPE_NC and snooped. clflush
	 * plus mfence makes the CPU stores visible before the doorbell. The
	 * pointer is the kernel area, never the user clone (SMAP).
	 */
	for (uint32 i = 0; i < in.ib_count; i++) {
		uint64 bytes = (uint64)ibs[i].dwords * 4;
		for (uint32 h = 0; h < client->handleCount; h++) {
			gpu_bo* bo = gpu_bo_get(client, client->handles[h]);
			if (bo == NULL || bo->domain != R5_GPU_DOMAIN_GTT
				|| bo->kernel == NULL)
				continue;
			for (uint32 r = 0; r < kMaxRange; r++) {
				gpu_range* range = &bo->ranges[r];
				if (!range->live)
					continue;
				if (ibs[i].gpu_va < range->va
					|| ibs[i].gpu_va + bytes > range->va + range->bytes)
					continue;
				uint8* cpu = (uint8*)bo->kernel + range->offset
					+ (ibs[i].gpu_va - range->va);
				uintptr_t start = (uintptr_t)cpu & ~(uintptr_t)63;
				uintptr_t end = ((uintptr_t)cpu + (uintptr_t)bytes + 63)
					& ~(uintptr_t)63;
				for (uintptr_t line = start; line < end; line += 64)
					asm volatile("clflush (%0)" :: "r"((void*)line)
						: "memory");
			}
		}
	}
	asm volatile("mfence" ::: "memory");
	if (gpu_debug_enabled()) {
		for (uint32 i = 0; i < in.ib_count; i++) {
			dprintf("radeon_hd: t %lld submit ib va %016llx dw %lu flags %lu\n",
				(long long)system_time(),
				(unsigned long long)ibs[i].gpu_va,
				(unsigned long)ibs[i].dwords,
				(unsigned long)ibs[i].flags);
		}
	}
	gpu_job* job = &sJobs[jobSlot];
	memset(job, 0, sizeof(*job));
	uint32 signalFlags[kMaxList];
	memset(signalFlags, 0, sizeof(signalFlags));
	for (uint32 i = 0; i < in.signal_count; i++) {
		if (signals[i].handle == 0 || signals[i].handle > kMaxSync)
			return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
				in.ib_count, 0, signals[i].handle, in.ring, signals[i].flags);
		gpu_sync* sync = &client->syncs[signals[i].handle - 1u];
		if (!sync->live)
			return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
				in.ib_count, 0, signals[i].handle, in.ring, signals[i].flags);
		job->syncs[i] = sync;
		job->points[i] = signals[i].point;
		signalFlags[i] = signals[i].flags;
	}
	job->count = in.signal_count;
	job->owner = client;
	uint64 sequence = 0;
	int fileHeld = 0;
	if (gR5GpuOps.set_debug == NULL || gR5GpuOps.set_job_pasid == NULL
		|| gR5GpuOps.user_submit == NULL || gR5GpuOps.user_seen == NULL
		|| gR5GpuOps.set_file_root == NULL || gR5GpuOps.set_shared_root == NULL
		|| gR5GpuOps.touch_root == NULL)
		return gpu_refuse("CS_SUBMIT", "ring", B_NOT_SUPPORTED, in.context, 0,
			in.ib_count, ibs[0].gpu_va, 0, in.ring, 0);
	gR5GpuOps.set_debug(gpu_debug_enabled() ? 1 : 0);
	gR5GpuOps.set_job_pasid(client->pasid, (uint64)(addr_t)client);
	if (client->contexts <= 0 || client->fenceCtx == 0)
		return gpu_refuse("CS_SUBMIT", "context", B_BAD_VALUE, in.context, 0,
			in.ib_count, 0, 0, in.ring, 0);
	uint64 vas[kMaxList];
	uint32 dws[kMaxList];
	uint32 vmids[kMaxList];
	uint32 ibFlags[kMaxList];
	for (uint32 i = 0; i < in.ib_count; i++) {
		vas[i] = ibs[i].gpu_va;
		dws[i] = ibs[i].dwords;
		vmids[i] = 1;
		ibFlags[i] = ibs[i].flags;
	}
	if (client->tree != NULL) {
		int flush = 0;
		uint32 fileVmid = r5_vmid_reserve(&sFileVmids, client->tree->owner,
			gpu_hw_seen(), client->tree->vm.used, &flush);
		if (fileVmid == 0)
			return gpu_refuse("CS_SUBMIT", "vmid", B_BUSY, in.context, 0,
				in.ib_count, 0, 0, in.ring, 0);
		fileHeld = 1;
		(void)flush;
		for (uint32 i = 0; i < in.ib_count; i++)
			vmids[i] = fileVmid;
		gR5GpuOps.set_file_root(client->tree->vm.pages[0].physical);
		gR5GpuOps.touch_root();
	}
	if (!gR5GpuOps.user_submit(vas, dws, vmids, ibFlags, in.ib_count,
			client->fenceCtx, &client->preamblePresented, &sequence)) {
		if (fileHeld) {
			r5_vmid_cancel(&sFileVmids);
			gR5GpuOps.set_shared_root();
			gR5GpuOps.touch_root();
		}
		return gpu_refuse("CS_SUBMIT", "ring", B_ERROR, in.context, 0,
			in.ib_count, ibs[0].gpu_va, 0, in.ring, 0);
	}
	if (fileHeld) {
		r5_vmid_arm(&sFileVmids, sequence);
		gR5GpuOps.set_shared_root();
		gR5GpuOps.touch_root();
	}
	job->sequence = sequence;
	job->submitted = system_time();
	if (in.ib_count > 0)
		sLastIbVa = ibs[0].gpu_va;
	sLastIbN = in.ib_count > 8 ? 8 : in.ib_count;
	for (uint32 i = 0; i < sLastIbN; i++) {
		sLastIbList[i] = ibs[i].gpu_va;
		sLastIbDw[i] = ibs[i].dwords;
	}
	if (sequence > sHighestSequence)
		sHighestSequence = sequence;
	for (uint32 i = 0; i < in.signal_count; i++) {
		/* RADV's queue syncobj is signaled with timeline point 0.
		 * That is this job's sequence, the same as a binary signal.
		 * submitted moves here. point moves when the user fence
		 * shows this sequence. */
		if ((signalFlags[i] & R5_GPU_CS_SYNC_BINARY) != 0
			|| job->points[i] == 0)
			job->points[i] = sequence;
		gpu_sync* sync = job->syncs[i];
		if (sync != NULL && job->points[i] > sync->submitted)
			sync->submitted = job->points[i];
	}
	job->live = true;
	client->lastSequence = sequence;
	if (gpu_hw_seen() >= sequence)
		r5_gpu_eop();
	in.sequence = sequence;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "copy", status, in.context, 0, in.ib_count,
			ibs[0].gpu_va, 0, in.ring, 0);
	return B_OK;
}


static status_t
gpu_fence_wait(void* info, void* buffer, size_t length)
{
	r5_gpu_fence in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	status = gpu_wait_value(info, NULL, in.sequence, in.timeout_us);
	if (status == B_TIMED_OUT && in.timeout_us != 0)
		gpu_hw_dump("fence timeout");
	return status;
}


static gpu_sync*
gpu_sync_get(gpu_client* client, uint32 handle)
{
	if (handle == 0 || handle > kMaxSync)
		return NULL;
	gpu_sync* sync = &client->syncs[handle - 1u];
	if (!sync->live || sync->owner != client)
		return NULL;
	return sync;
}


static status_t
gpu_sync_create(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	int slot = -1;
	for (uint32 i = 0; i < kMaxSync; i++) {
		if (!client->syncs[i].live) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0) {
		if (gpu_error_line())
			dprintf("radeon_hd: syncobj limit\n");
		return B_NO_MEMORY;
	}
	client->syncs[slot].live = true;
	client->syncs[slot].point = (in.flags & R5_GPU_SYNCOBJ_SIGNALED) != 0
		? 1 : 0;
	client->syncs[slot].submitted = client->syncs[slot].point;
	client->syncs[slot].owner = client;
	in.handle = (uint32)slot + 1u;
	return gpu_copy_out(buffer, length, &in, sizeof(in));
}


static status_t
gpu_sync_destroy(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	gpu_sync* sync = gpu_sync_get(client, in.handle);
	if (sync == NULL)
		return B_BAD_VALUE;
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (!sJobs[i].live)
			continue;
		for (uint32 n = 0; n < sJobs[i].count; n++) {
			if (sJobs[i].syncs[n] == sync)
				sJobs[i].syncs[n] = NULL;
		}
	}
	sync->live = false;
	sync->owner = NULL;
	return B_OK;
}


static status_t
gpu_sync_wait(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	gpu_sync* sync = gpu_sync_get(client, in.handle);
	if (sync == NULL)
		return B_BAD_VALUE;
	if ((in.flags & R5_GPU_SYNCOBJ_BINARY) != 0) {
		if (sync->submitted == 0)
			return B_TIMED_OUT;
		status = gpu_wait_value(client->info, &sync->point, sync->submitted,
			in.timeout_us);
	} else
		status = gpu_wait_value(client->info, &sync->point, in.point,
			in.timeout_us);
	if (status == B_TIMED_OUT && in.timeout_us != 0)
		gpu_hw_dump("sync wait");
	return status;
}


static bool
gpu_job_blocks(const gpu_sync* sync, uint64 target, bool binary)
{
	uint64 seen = gpu_hw_seen();
	for (uint32 i = 0; i < kMaxJob; i++) {
		gpu_job* job = &sJobs[i];
		if (!job->live || job->sequence <= seen)
			continue;
		for (uint32 n = 0; n < job->count; n++) {
			if (job->syncs[n] != sync)
				continue;
			if (binary)
				return true;
			if (job->points[n] >= target && sync->point < target)
				return true;
		}
	}
	return false;
}


static bool
gpu_list_ready(const gpu_sync* sync, uint64 target, uint32 flags)
{
	bool binary = (flags & R5_GPU_WAIT_BINARY) != 0;
	bool available = (flags & R5_GPU_WAIT_AVAILABLE) != 0;
	if (!available && gpu_job_blocks(sync, target, binary))
		return false;
	uint64 value = available ? sync->submitted : sync->point;
	return gpu_sync_signalled(value, sync->submitted, target, binary);
}


static bool
gpu_list_submitted(const gpu_sync* sync, uint64 target, uint32 flags)
{
	if ((flags & R5_GPU_WAIT_BINARY) != 0)
		return sync->submitted > 0;
	return sync->submitted >= target;
}


static status_t
gpu_sync_wait_list(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj_wait in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	if (in.count == 0 || in.count > kMaxSync)
		return B_BAD_VALUE;
	uint32 handles[kMaxSync];
	uint64 points[kMaxSync];
	memset(handles, 0, sizeof(handles));
	memset(points, 0, sizeof(points));
	status = gpu_copy_user(handles, in.handles, sizeof(uint32) * in.count);
	if (status != B_OK)
		return status;
	if (in.points != 0) {
		status = gpu_copy_user(points, in.points, sizeof(uint64) * in.count);
		if (status != B_OK)
			return status;
	}
	gpu_sync* syncs[kMaxSync];
	for (uint32 i = 0; i < in.count; i++) {
		syncs[i] = gpu_sync_get(client, handles[i]);
		if (syncs[i] == NULL)
			return B_BAD_VALUE;
	}
	/* Once, over every handle, before any signalled handle can end the
	 * scan. Linux drm_syncobj_array_wait_timeout does the same. A reset
	 * during the wait is a timeout, not a second EINVAL.
	 */
	if ((in.flags & R5_GPU_WAIT_FOR_SUBMIT) == 0) {
		for (uint32 i = 0; i < in.count; i++) {
			if (!gpu_list_submitted(syncs[i], points[i], in.flags))
				return B_BAD_VALUE;
		}
	}
	bigtime_t deadline = system_time() + (bigtime_t)in.timeout_us;
	while (true) {
		int32 first = -1;
		uint32 met = 0;
		for (uint32 i = 0; i < in.count; i++) {
			if (!syncs[i]->live)
				return B_BAD_VALUE;
			if (!gpu_list_ready(syncs[i], points[i], in.flags))
				continue;
			met++;
			if (first < 0)
				first = (int32)i;
			if ((in.flags & R5_GPU_WAIT_ALL) == 0)
				break;
		}
		if ((in.flags & R5_GPU_WAIT_ALL) != 0 && met == in.count) {
			in.first_signaled = 0;
			return gpu_copy_out(buffer, length, &in, sizeof(in));
		}
		if ((in.flags & R5_GPU_WAIT_ALL) == 0 && first >= 0) {
			in.first_signaled = (uint32)first;
			return gpu_copy_out(buffer, length, &in, sizeof(in));
		}
		bigtime_t now = system_time();
		if (sLost)
			return ECANCELED;
		if (in.timeout_us == 0 || now >= deadline)
			return B_TIMED_OUT;
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		gpu_unlock();
		status_t waited = entry.Wait(B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT,
			deadline - now);
		gpu_lock();
		if (waited == B_INTERRUPTED || waited == B_WOULD_BLOCK)
			return waited;
	}
}


static void
gpu_sync_set(gpu_sync* sync, uint64 point)
{
	if (point > sync->submitted)
		sync->submitted = point;
	if (point > sync->point)
		sync->point = point;
	sEop.NotifyAll();
}


static status_t
gpu_sync_signal(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	gpu_sync* sync = gpu_sync_get(client, in.handle);
	if (sync == NULL)
		return B_BAD_VALUE;
	if ((in.flags & R5_GPU_SYNCOBJ_BINARY) != 0) {
		gpu_sync_host(sync);
		return B_OK;
	}
	gpu_sync_set(sync, in.point);
	return B_OK;
}


static bool
gpu_src_reached(const gpu_sync* sync, uint64 srcPoint)
{
	bool binary = srcPoint == 0;
	if (gpu_job_blocks(sync, srcPoint, binary))
		return false;
	return gpu_sync_signalled(sync->point, sync->submitted, srcPoint, binary);
}


static void
gpu_sync_host(gpu_sync* sync)
{
	uint64 stamp = sync->submitted;
	if (sync->point > stamp)
		stamp = sync->point;
	uint64 high = sHighestSequence == 0 ? 1 : sHighestSequence;
	if (high > stamp)
		stamp = high;
	sync->submitted = stamp;
	sync->point = stamp;
	sEop.NotifyAll();
}


static status_t
gpu_sync_transfer(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj_transfer in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	if (in.flags != 0)
		return B_BAD_VALUE;
	gpu_sync* src = gpu_sync_get(client, in.src_handle);
	gpu_sync* dst = gpu_sync_get(client, in.dst_handle);
	if (src == NULL || dst == NULL)
		return B_ENTRY_NOT_FOUND;
	bool srcBinary = in.src_point == 0;
	bool dstBinary = in.dst_point == 0;
	if (srcBinary && src->submitted == 0)
		return B_BAD_VALUE;
	if (!gpu_src_reached(src, in.src_point) && sLost)
		return ECANCELED;
	if (gpu_src_reached(src, in.src_point)) {
		if (dstBinary)
			gpu_sync_host(dst);
		else
			gpu_sync_set(dst, in.dst_point);
		return B_OK;
	}
	gpu_job* job = NULL;
	uint64 lowest = 0;
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (!sJobs[i].live)
			continue;
		for (uint32 n = 0; n < sJobs[i].count; n++) {
			if (sJobs[i].syncs[n] != src)
				continue;
			if (srcBinary) {
				if (sJobs[i].points[n] == src->submitted)
					job = &sJobs[i];
			} else if (sJobs[i].points[n] >= in.src_point
				&& (job == NULL || sJobs[i].points[n] < lowest)) {
				job = &sJobs[i];
				lowest = sJobs[i].points[n];
			}
		}
	}
	if (job == NULL)
		return B_BAD_VALUE;
	uint64 dstPoint = dstBinary ? job->sequence : in.dst_point;
	for (uint32 n = 0; n < job->count; n++) {
		if (job->syncs[n] != dst)
			continue;
		if (dstPoint > job->points[n])
			job->points[n] = dstPoint;
		if (dstPoint > dst->submitted)
			dst->submitted = dstPoint;
		return B_OK;
	}
	if (job->count >= kMaxList)
		return B_NO_MEMORY;
	job->syncs[job->count] = dst;
	job->points[job->count] = dstPoint;
	job->count++;
	if (dstPoint > dst->submitted)
		dst->submitted = dstPoint;
	return B_OK;
}


bool
gpu_selftest_enabled()
{
	if (sSelftestOn < 0)
		gpu_cache_log_settings();
	return sSelftestOn != 0;
}


uint32
gpu_known_dw(uint64 va)
{
	for (uint32 i = 0; i < sLastIbN && i < 8; i++) {
		if (sLastIbList[i] == va && sLastIbDw[i] != 0)
			return sLastIbDw[i];
	}
	return 0;
}


static status_t
gpu_open(const char* name, uint32 flags, void** cookie)
{
	(void)name;
	(void)flags;
	if (gR5GpuOps.device == NULL)
		return B_NOT_SUPPORTED;
	void* device = gR5GpuOps.device();
	if (!sReady || device == NULL)
		return B_ERROR;
	gpu_client* client = (gpu_client*)malloc(sizeof(gpu_client));
	if (client == NULL)
		return B_NO_MEMORY;
	memset(client, 0, sizeof(*client));
	client->info = device;
	gpu_lock();
	int pasid = r5_pasid_alloc(&sPasids, 16);
	if (pasid < 0) {
		gpu_unlock();
		free(client);
		return B_NO_MEMORY;
	}
	client->pasid = (uint32)pasid;
	client->next = sClientList;
	sClientList = client;
	/* The file tree is published before the cookie leaves the lock. */
	status_t root = gpu_file_root(client);
	if (root != B_OK) {
		sClientList = client->next;
		r5_pasid_free(&sPasids, client->pasid);
		gpu_unlock();
		free(client);
		return root;
	}
	gpu_unlock();
	*cookie = client;
	return B_OK;
}


static status_t
gpu_close(void* cookie)
{
	(void)cookie;
	return B_OK;
}


static status_t
gpu_file_root(gpu_client* client)
{
	int perFile = gpu_hw_per_file();
	if (perFile < 0 || gR5GpuOps.vmid_prepare == NULL
		|| gR5GpuOps.file_vm_init == NULL)
		return B_NOT_SUPPORTED;
	if (!perFile || client == NULL)
		return B_OK;
	if (client->tree != NULL)
		return B_OK;
	if (!gpu_hw_prepare())
		return B_NO_INIT;
	r5_file_tree* tree = (r5_file_tree*)malloc(sizeof(r5_file_tree));
	if (tree == NULL)
		return B_NO_MEMORY;
	memset(tree, 0, sizeof(*tree));
	tree->pagesN = 256;
	tree->pages = (r5_vm_page*)malloc(sizeof(r5_vm_page) * tree->pagesN);
	if (tree->pages == NULL) {
		free(tree);
		return B_NO_MEMORY;
	}
	if (!gR5GpuOps.file_vm_init(&tree->vm, tree->pages, tree->pagesN)) {
		free(tree->pages);
		free(tree);
		return B_NO_MEMORY;
	}
	if (!sFileVmidsReady) {
		r5_vmid_init_files(&sFileVmids);
		sFileVmidsReady = true;
	}
	tree->owner = sNextFileOwner;
	sNextFileOwner++;
	if (sNextFileOwner == 0)
		sNextFileOwner = 1;
	r5_file_count_open(&tree->count, (int32)tree->pagesN);
	client->tree = tree;
	return B_OK;
}


static status_t
gpu_get_counters(gpu_client* client, void* buffer, size_t length)
{
	if (client == NULL || buffer == NULL || length < sizeof(r5_gpu_counters))
		return B_BAD_VALUE;
	if (!is_user_address_range(buffer, sizeof(r5_gpu_counters)))
		return B_BAD_ADDRESS;
	r5_gpu_counters out;
	memset(&out, 0, sizeof(out));
	out.size = (uint32)sizeof(out);
	out.version = 1;
	if (gR5GpuOps.shared_vm == NULL)
		return B_NOT_SUPPORTED;
	gpu_lock();
	r5_vm* shared = gpu_hw_shared();
	uint32 sharedPages = shared != NULL ? shared->used : 0;
	uint32 trees = 0;
	uint32 treePages = 0;
	uint32 treeUsed = 0;
	for (gpu_client* scan = sClientList; scan != NULL; scan = scan->next) {
		if (scan->tree == NULL)
			continue;
		trees++;
		treePages += scan->tree->pagesN;
		treeUsed += scan->tree->vm.used;
	}
	for (uint32 i = 0; i < kMaxBo; i++) {
		if (!sBo[i].live)
			continue;
		out.bo_count++;
		if (sBo[i].owner == client)
			out.bo_count_space++;
	}
	out.pt_pages = sharedPages + treeUsed;
	out.vmids = 1u + trees;
	out.trees = trees;
	out.tree_pages = treePages;
	if (client->tree != NULL) {
		out.pt_pages_space = client->tree->vm.used;
		out.vmids_space = 1;
		out.trees_space = 1;
		out.tree_pages_space = client->tree->pagesN;
	} else
		out.pt_pages_space = sharedPages;
	gpu_unlock();
	if (user_memcpy(buffer, &out, sizeof(out)) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static status_t
gpu_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	gpu_client* client = (gpu_client*)cookie;
	if (client == NULL || client->info == NULL)
		return B_BAD_VALUE;
	if (op == R5_GPU_GET_INFO) {
		if (gR5GpuOps.get_info == NULL)
			return B_NOT_SUPPORTED;
		return gR5GpuOps.get_info(client, buffer, length);
	}
	if (op == R5_GPU_GET_COUNTERS)
		return gpu_get_counters(client, buffer, length);
	if (!gpu_render_enabled())
		return B_NOT_ALLOWED;
	if (op == R5_GPU_BO_EXPORT || op == R5_GPU_BO_IMPORT)
		return B_UNSUPPORTED;

	gpu_lock();
	sInfo = client->info;
	int perFile = gpu_hw_per_file();
	if (perFile < 0) {
		gpu_unlock();
		return B_NOT_SUPPORTED;
	}
	if (perFile) {
		status_t root = gpu_file_root(client);
		if (root != B_OK) {
			gpu_unlock();
			return root;
		}
	}
	status_t status = B_BAD_VALUE;
	if (op == R5_GPU_READ_REG) {
		r5_gpu_read_reg in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (gR5GpuOps.read_reg == NULL)
			status = B_NOT_SUPPORTED;
		else {
			status = gR5GpuOps.read_reg(client->info, in.offset, &in.value);
			if (status == B_OK)
				status = gpu_copy_out(buffer, length, &in, sizeof(in));
		}
	} else if (op == R5_GPU_CTX_CREATE) {
		r5_gpu_ctx in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (gR5GpuOps.vmid_prepare == NULL)
			status = B_NOT_SUPPORTED;
		else if (!gpu_hw_prepare())
			status = B_NO_INIT;
		else {
			if (sContexts > 0)
				if (gpu_debug_enabled())
				dprintf("radeon_hd: t %lld vmid1 reused, clients %ld\n",
					(long long)system_time(), (long)sContexts);
			sContexts++;
			if (client->contexts == 0)
				client->fenceCtx = r5_ctx_handout(&sCtxCounter);
			client->contexts++;
			in.context = 1;
			status = gpu_copy_out(buffer, length, &in, sizeof(in));
		}
	} else if (op == R5_GPU_CTX_DESTROY) {
		r5_gpu_ctx in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (in.context != 1 || client->contexts <= 0)
			status = B_BAD_VALUE;
		else {
			client->contexts--;
			sContexts--;
			status = B_OK;
		}
	} else if (op == R5_GPU_BO_CREATE)
		status = gpu_bo_create(client, buffer, length);
	else if (op == R5_GPU_BO_FREE)
		status = gpu_bo_free(client, buffer, length);
	else if (op == R5_GPU_BO_MAP)
		status = gpu_bo_map(client, buffer, length);
	else if (op == R5_GPU_VA_MAP)
		status = gpu_va_map(client, buffer, length);
	else if (op == R5_GPU_VA_UNMAP)
		status = gpu_va_unmap(client, buffer, length);
	else if (op == R5_GPU_VA_REPLACE)
		status = gpu_va_replace(client, buffer, length, false);
	else if (op == R5_GPU_STAGE_P) {
		r5_gpu_stage_p in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status == B_OK) {
			if (gR5GpuOps.stage_p == NULL)
				status = B_NOT_SUPPORTED;
			else
				status = gR5GpuOps.stage_p(client->info, &in);
			status_t copied = gpu_copy_out(buffer, length, &in, sizeof(in));
			if (copied != B_OK)
				status = copied;
		}
	} else if (op == R5_GPU_DEBUG_SWITCH) {
		if (gR5GpuOps.debug_switch == NULL)
			status = B_NOT_SUPPORTED;
		else
			status = gR5GpuOps.debug_switch(buffer, length);
	}
	else if (op == R5_GPU_LOG_REGS) {
		r5_gpu_log_regs in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (in.version != 1)
			status = B_BAD_VALUE;
		else {
			if (gR5GpuOps.log_state == NULL)
				status = B_NOT_SUPPORTED;
			else {
				gR5GpuOps.log_state("selftest", in.line, sizeof(in.line));
			in.length = (uint32)strlen(in.line);
				status = gpu_copy_out(buffer, length, &in, sizeof(in));
			}
		}
	} else if (op == R5_GPU_STAGE_R1) {
		r5_gpu_stage_r1 in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (in.version != 1
			|| (in.which != R5_GPU_R1_AB && in.which != R5_GPU_R1_C)
			|| r5_vm_va_fits(in.x, 4096) == 0
			|| in.y != in.x + 0x1000ull
			|| r5_vm_va_fits(in.y, 4096) == 0)
			status = B_BAD_VALUE;
		else if (sLost) {
			dprintf("radeon_hd: skipped, gpu lost\n");
			status = ECANCELED;
		} else if (!gpu_render_enabled() || !gpu_selftest_enabled())
			status = B_NOT_ALLOWED;
		else if (gR5GpuOps.vmid_prepare == NULL || gR5GpuOps.stage_r1 == NULL) {
			status = B_NOT_SUPPORTED;
		} else if (!gpu_hw_prepare()) {
			dprintf("radeon_hd: r1 skip vmid\n");
			status = B_NO_INIT;
		} else {
			gR5GpuOps.stage_r1(client->info, in.which, in.x, in.y,
				&in.old_word, &in.new_word);
			status = sLost ? ECANCELED : B_OK;
			status_t copied = gpu_copy_out(buffer, length, &in, sizeof(in));
			if (copied != B_OK)
				status = copied;
		}
	}
	else if (op == R5_GPU_SELFTEST) {
		if (sLost) {
			dprintf("radeon_hd: skipped, gpu lost\n");
			status = ECANCELED;
		} else if (!gpu_selftest_enabled())
			status = B_NOT_ALLOWED;
		else
			if (gR5GpuOps.stage_u == NULL)
				status = B_NOT_SUPPORTED;
			else
				status = gR5GpuOps.stage_u(client->info);
	} else if (op == R5_GPU_VA_CLEAR)
		status = gpu_va_replace(client, buffer, length, true);
	else if (op == R5_GPU_CS_SUBMIT)
		status = gpu_submit(client, buffer, length);
	else if (op == R5_GPU_FENCE_WAIT)
		status = gpu_fence_wait(client->info, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_CREATE)
		status = gpu_sync_create(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_DESTROY)
		status = gpu_sync_destroy(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_WAIT)
		status = gpu_sync_wait(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_WAIT_LIST)
		status = gpu_sync_wait_list(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_SIGNAL)
		status = gpu_sync_signal(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_TRANSFER)
		status = gpu_sync_transfer(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_RESET || op == R5_GPU_SYNCOBJ_QUERY) {
		r5_gpu_syncobj in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status == B_OK) {
			gpu_sync* sync = gpu_sync_get(client, in.handle);
			if (sync == NULL)
				status = B_BAD_VALUE;
			else if (op == R5_GPU_SYNCOBJ_RESET) {
				sync->point = 0;
				sync->submitted = 0;
				status = B_OK;
			} else {
				in.point = sync->point;
				status = gpu_copy_out(buffer, length, &in, sizeof(in));
			}
		}
	} 	else
		status = B_UNSUPPORTED;
	gpu_unlock();
	return status;
}



/* vm.h pulls thread types in before the mutex type. These are the calls
 * radeon_hd already resolves from the kernel, same as vm_set_area_memory_type.
 */
extern "C" area_id vm_clone_area(team_id team, const char* name, void** address,
	uint32 addressSpec, uint32 protection, uint32 mapping, area_id sourceArea,
	bool kernel);
extern "C" team_id team_get_current_team_id(void);

/* headers/private/system/vm_defs.h: REGION_NO_PRIVATE_MAP = 0. */
static const uint32 kRegionNoPrivateMap = 0;


static void
r5_gpu_eop()
{
	if (!sReady)
		return;
	uint64 seen = gpu_hw_seen();
	for (uint32 i = 0; i < kMaxJob; i++) {
		gpu_job* job = &sJobs[i];
		if (!job->live || job->sequence > seen)
			continue;
		for (uint32 n = 0; n < job->count; n++) {
			gpu_sync* sync = job->syncs[n];
			if (sync != NULL && sync->live && job->points[n] > sync->point)
				sync->point = job->points[n];
		}
		job->live = false;
	}
	sEop.NotifyAll();
}


static uint64
gpu_counter(volatile uint64* value)
{
	if (value == NULL)
		return gpu_hw_seen();
	return *value;
}


static status_t
gpu_wait_value(void* info, volatile uint64* value, uint64 target,
	uint64 timeoutUs)
{
	(void)info;
	int step = r5_wait_step(gpu_counter(value), target, timeoutUs, 0);
	if (step == R5_WAIT_DONE)
		return B_OK;
	if (sLost)
		return ECANCELED;
	if (step == R5_WAIT_POLL)
		return B_TIMED_OUT;
	bigtime_t deadline = system_time() + (bigtime_t)timeoutUs;
	while (true) {
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		bigtime_t now = system_time();
		step = r5_wait_step(gpu_counter(value), target, timeoutUs,
			now >= deadline);
		if (step == R5_WAIT_DONE)
			return B_OK;
		if (sLost)
			return ECANCELED;
		if (step == R5_WAIT_TIMEOUT)
			return B_TIMED_OUT;
		bigtime_t left = deadline - now;
		gpu_unlock();
		entry.Wait(B_RELATIVE_TIMEOUT, left);
		gpu_lock();
	}
}


static void
gpu_fault_retire()
{
	uint32 pasid = 0;
	uint32 vmid = 0;
	uint64 addr = 0;
	while (gR5GpuOps.fault_take != NULL
		&& gR5GpuOps.fault_take(&pasid, &vmid, &addr)) {
		gpu_client* found = NULL;
		for (gpu_client* scan = sClientList; scan != NULL; scan = scan->next) {
			if (scan->pasid != 0 && scan->pasid == pasid) {
				found = scan;
				break;
			}
		}
		if (found == NULL) {
			dprintf("radeon_hd: vm fault pasid %lu unknown addr %016llx\n",
				(unsigned long)pasid, (unsigned long long)addr);
			continue;
		}
		found->faulted = true;
		if (sGuilty == NULL)
			sGuilty = found;
		dprintf("radeon_hd: vm fault pasid %lu vmid %lu addr %016llx\n",
			(unsigned long)pasid, (unsigned long)vmid,
			(unsigned long long)addr);
	}
}


static status_t
gpu_bo_map(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_MAP", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return gpu_refuse("BO_MAP", "bo", B_BAD_VALUE, in.handle, 0, 0, 0, 0, 0,
			0);
	/* Another team does not get a clone until M5. The same team gets
	 * the clone it already has. clone_area() would put user pages in the
	 * kernel address space and add the kernel bits (SMAP). vm_clone_area
	 * with kernel true puts only the user bits in this team. The kernel
	 * does not read or write through that clone.
	 */
	team_id team = team_get_current_team_id();
	if (bo->area >= 0) {
		if (bo->mapTeam != team)
			return gpu_refuse("BO_MAP", "team", B_NOT_ALLOWED, in.handle,
				bo->domain, bo->bytes, 0, bo->vramOffset, 0, bo->flags);
		in.area = bo->area;
		return gpu_copy_out(buffer, length, &in, sizeof(in));
	}
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		void* kernel = NULL;
		if (gR5GpuOps.map_vram == NULL)
			return gpu_refuse("BO_MAP", "vram", B_NOT_SUPPORTED, in.handle,
				bo->domain, bo->bytes, 0, bo->vramOffset, 0, bo->flags);
		area_id kernelArea = gR5GpuOps.map_vram(bo->vramOffset, bo->bytes,
			&kernel);
		if (kernelArea < 0 || kernel == NULL)
			return gpu_refuse("BO_MAP", "vram", kernelArea, in.handle, bo->domain,
				bo->bytes, 0, bo->vramOffset, 0, bo->flags);
		bo->kernelArea = kernelArea;
		bo->kernel = kernel;
	}
	void* user = NULL;
	area_id area = vm_clone_area(team, "r5 gpu bo", &user, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, kRegionNoPrivateMap, bo->kernelArea,
		true);
	if (area < 0)
		return gpu_refuse("BO_MAP", "clone", area, in.handle, bo->domain,
			bo->bytes, 0, bo->vramOffset, 0, bo->flags);
	bo->area = area;
	bo->mapTeam = team;
	in.area = area;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_MAP", "copy", status, in.handle, bo->domain,
			bo->bytes, 0, bo->vramOffset, 0, bo->flags);
	return B_OK;
}


static status_t
gpu_free(void* cookie)
{
	gpu_client* client = (gpu_client*)cookie;
	if (client == NULL)
		return B_OK;
	gpu_lock();
	status_t status = B_OK;
	uint64 seen = gpu_hw_seen();
	r5_gpu_eop();
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (!sJobs[i].live)
			continue;
		for (uint32 n = 0; n < sJobs[i].count; n++) {
			gpu_sync* sync = sJobs[i].syncs[n];
			if (sync != NULL && sync->owner == client)
				sJobs[i].syncs[n] = NULL;
		}
	}
	for (uint32 i = 0; i < client->handleCount; i++) {
		uint32 handle = client->handles[i];
		if (handle == 0 || handle > kMaxBo)
			continue;
		gpu_bo* bo = &sBo[handle - 1u];
		if (!bo->live || bo->owner != client)
			continue;
		uint64 sequence = client->lastSequence;
		if (!sLost && sequence != 0 && seen < sequence) {
			bo->owner = NULL;
			bo->parked = true;
			bo->parkSequence = sequence;
			gpu_bo_hold_tree(bo);
		} else
			gpu_bo_release(bo);
	}
	for (uint32 i = 0; i < kMaxSync; i++) {
		client->syncs[i].live = false;
		client->syncs[i].owner = NULL;
	}
	if (client->pasid != 0)
		r5_pasid_free(&sPasids, client->pasid);
	if (client->tree != NULL) {
		r5_file_tree* tree = client->tree;
		client->tree = NULL;
		tree->seen = seen;
		r5_file_tree_drop(tree);
	}
	if (sClientList == client)
		sClientList = client->next;
	else {
		for (gpu_client* scan = sClientList; scan != NULL; scan = scan->next) {
			if (scan->next == client) {
				scan->next = client->next;
				break;
			}
		}
	}
	if (client->contexts > 0) {
		sContexts -= client->contexts;
		if (sContexts < 0)
			sContexts = 0;
	}
	gpu_unlock();
	free(client);
	return status;
}


static status_t
gpu_read(void* cookie, off_t position, void* buffer, size_t* numBytes)
{
	(void)cookie;
	(void)position;
	(void)buffer;
	*numBytes = 0;
	return B_ERROR;
}


static status_t
gpu_write(void* cookie, off_t position, const void* buffer, size_t* numBytes)
{
	(void)cookie;
	(void)position;
	(void)buffer;
	*numBytes = 0;
	return B_ERROR;
}


device_hooks gR5GpuHooks = {
	gpu_open,
	gpu_close,
	gpu_free,
	gpu_control,
	gpu_read,
	gpu_write,
	NULL,
	NULL,
	NULL,
	NULL
};
