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
 * stays in the ring past the GFX watchdog (10 seconds, the same as
 * amdgpu.lockup_timeout) is a hang. A client wait is not. The hang
 * policy is r5_hang.h: the first hang resets the GPU (mode 2) and runs
 * the fixed sequences and the self-test again; every context of that
 * moment is lost. A second hang, or a failed reset or self-test, turns
 * the GPU off until the next boot.
 */

#include "r5_gpu_internal.h"
#include "r5_gpu_checks.h"
#include "r5_cs.h"
#include "r5_defer.h"
#include "r5_hang.h"
#include "r5_log.h"
#include "r5_span.h"
#include "r5_vmid.h"
#include "r5_wait.h"

#include <KernelExport.h>
#include <OS.h>
#include <kernel.h>
#include <Drivers.h>

#include <driver_settings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


extern "C" status_t vm_delete_area(team_id teamID, area_id areaID, bool kernel);
extern "C" bool team_is_valid(team_id id);
extern "C" status_t vm_set_area_page_memory_type(area_id id, uint32 type);
extern "C" team_id team_get_current_team_id(void);
/* src/system/kernel/util/Random.cpp (headers/private/kernel/util/Random.h). */
extern "C" unsigned int secure_random_value(void);

static void r5_gpu_eop();
static void gpu_fault_retire();
static status_t gpu_wait_value(gpu_client* client, volatile uint64* value,
	uint64 target, uint64 timeoutUs);
static status_t gpu_bo_map(gpu_client* client, void* buffer, size_t length);
static status_t gpu_free(void* cookie);
static status_t gpu_read(void* cookie, off_t position, void* buffer, size_t* numBytes);
static status_t gpu_write(void* cookie, off_t position, const void* buffer,
	size_t* numBytes);


int sErrorLines;
int sOnceBits;
int sDebugOn = -1;
int sSelftestOn = -1;
int sSubmitLogOn = -1;
int sDeferred = 0;
static r5_defer sDefer[256];

gpu_bo sBo[kMaxBo];
gpu_backing sBacking[kMaxBo];
/* M11 step 3b: the export counter. Only goes up; 0 is never handed out. */
static uint64 sShareCounter = 0;
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
bool sGpuOff = false;
bool sResetting = false;
uint32 sHangTest = 0;
static r5_hang sHang;
static bool sHangTestArmed = false;
static uint32 sHangTestJobs = 0;
static bigtime_t sFaultLineAt = 0;
/* The last mode-2 reset worked: the CP no longer reads old buffers. */
static bool sCpStopped = false;
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
static void gpu_hang_test_step();
static void gpu_wait_reset();
static bool gpu_caller_is_app_server();
static status_t gpu_bo_fill(gpu_client* client, gpu_bo* bo, r5_gpu_bo* request,
	uint64 bytes);
static void gpu_share_purge(bigtime_t now);



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
gpu_submit_log_enabled()
{
	if (sSubmitLogOn < 0)
		gpu_cache_log_settings();
	return sSubmitLogOn != 0;
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
	void* settings = load_driver_settings("radeon_hd");
	bool enabled = r5_gpu_switch_named(settings, "gpu_render") != 0;
	if (settings != NULL)
		unload_driver_settings(settings);
	return enabled;
}


// #pragma mark - M10 which path is used


static uint32 sLevel = R5_GPU_LEVEL_NONE;
static int sOptOut = 0;
bool sAvailable = false;


extern "C" void
r5_gpu_set_level(uint32_t level, int optOut)
{
	sLevel = level;
	sOptOut = optOut ? 1 : 0;
	/* r5_gpu_init read the cached switches before the level was known. */
	gpu_cache_log_settings();
}


extern "C" int
r5_gpu_switch_named(void* settings, const char* name)
{
	int named = -1;
	if (settings != NULL) {
		/* Named when the two possible defaults give different answers. */
		bool ifOff = get_driver_boolean_parameter(settings, name, false, true);
		bool ifOn = get_driver_boolean_parameter(settings, name, true, true);
		if (ifOff == ifOn)
			named = ifOn ? 1 : 0;
	}
	return r5_gpu_switch(sLevel, sOptOut, named);
}


extern "C" void
r5_gpu_report_checks(int switchedOff, int firmwareOk, int selftestOk)
{
	const char* reason = r5_gpu_check_reason(sLevel, sOptOut, switchedOff,
		firmwareOk, selftestOk);
	gpu_lock();
	sAvailable = reason == NULL && !sGpuOff;
	/* Refuse new contexts and submits, as after a second hang. A GPU
	 * that was never release-supported keeps its development path. */
	if (reason != NULL && sLevel == R5_GPU_LEVEL_RELEASE)
		sGpuOff = true;
	gpu_unlock();
	if (reason == NULL)
		dprintf("radeon_hd: gpu available\n");
	else
		dprintf("radeon_hd: gpu not available: %s\n", reason);
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
		gpu_hang_test_step();
		gpu_unlock();
	}
	return B_OK;
}


static int32 sRetireWakes = 0;


extern "C" void
r5_gpu_retire_wake()
{
	atomic_add(&sRetireWakes, 1);
	if (sRetireSem >= 0)
		release_sem_etc(sRetireSem, 1, B_DO_NOT_RESCHEDULE);
}


extern "C" uint32
r5_gpu_retire_wakes()
{
	return (uint32)sRetireWakes;
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


/* M11 step 3b: a new backing with one reference, for the slot that
 * creates it. NULL when the table is full. */
static gpu_backing*
gpu_backing_take()
{
	for (uint32 i = 0; i < kMaxBo; i++) {
		gpu_backing* backing = &sBacking[i];
		if (backing->live)
			continue;
		memset(backing, 0, sizeof(*backing));
		backing->live = true;
		backing->refs = 1;
		backing->kernelArea = -1;
		return backing;
	}
	return NULL;
}


/* Drops one reference. The last one frees the VRAM (not the borrowed
 * framebuffer) and deletes the kernel area. Every user clone of that
 * area is gone by then: each slot deletes its own before it drops. */
static void
gpu_backing_drop(gpu_backing* backing)
{
	if (backing == NULL || !backing->live)
		return;
	if (!r5_share_ref_drop(&backing->refs))
		return;
	if (backing->domain == R5_GPU_DOMAIN_VRAM && !backing->borrowed) {
		if (!sDeferred || !r5_defer_add(sDefer, 256, backing->vramOffset))
			gpu_hw_vram_free(backing->vramOffset);
	}
	if (backing->kernelArea >= 0)
		delete_area(backing->kernelArea);
	backing->kernelArea = -1;
	backing->kernel = NULL;
	backing->live = false;
}


void
gpu_bo_release(gpu_bo* bo)
{
	if (!bo->live)
		return;
	gpu_bo_unmap(bo);
	/* The user clone belongs to mapTeam. A team that has already ended
	 * took its areas with it. team_is_valid is the check; do not delete
	 * an id in a team that is gone.
	 */
	if (bo->area >= 0 && team_is_valid(bo->mapTeam))
		vm_delete_area(bo->mapTeam, bo->area, true);
	gpu_backing* backing = bo->backing;
	bo->backing = NULL;
	gpu_backing_drop(backing);
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
}


/* M11 step 3b: a free slot for one more handle of this file, or -1.
 * The limits are r5_share_slot_check; a refusal logs one line per file
 * (docs/APP-SERVER-COMPOSITOR.md, "Limits on buffer slots"). */
static int
gpu_slot_take(gpu_client* client, int* reason)
{
	uint32 live = 0;
	int slot = -1;
	for (uint32 i = 0; i < kMaxBo; i++) {
		if (sBo[i].live)
			live++;
		else if (slot < 0)
			slot = (int)i;
	}
	/* The team info is read only when the reserve is all that is left. */
	int appServer = live >= kMaxBo - R5_SHARE_RESERVE
		&& gpu_caller_is_app_server() ? 1 : 0;
	*reason = r5_share_slot_check(live, client->handleCount, appServer);
	if (*reason == R5_SHARE_OK && slot >= 0)
		return slot;
	if (*reason == R5_SHARE_OK)
		*reason = R5_SHARE_SYSTEM;
	if (!client->limitLogged) {
		client->limitLogged = true;
		dprintf("radeon_hd: buffer limit %lu reached, team %ld\n",
			(unsigned long)r5_share_limit_of(*reason),
			(long)team_get_current_team_id());
	}
	return -1;
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
	uint64 bytes = (in.bytes + 4095ull) & ~4095ull;
	int reason = R5_SHARE_OK;
	int slot = gpu_slot_take(client, &reason);
	if (slot < 0)
		return gpu_refuse("BO_CREATE", r5_share_reason(reason), B_NO_MEMORY,
			0, in.domain, bytes, 0, 0, 0, in.flags);
	gpu_backing* backing = gpu_backing_take();
	if (backing == NULL)
		return gpu_refuse("BO_CREATE", "slots", B_NO_MEMORY, 0, in.domain,
			bytes, 0, 0, 0, in.flags);
	/* Until the slot is live, a refusal below gives the backing back. */
	gpu_bo* bo = &sBo[slot];
	memset(bo, 0, sizeof(*bo));
	bo->area = -1;
	bo->mapTeam = -1;
	bo->backing = backing;
	backing->domain = in.domain;
	backing->flags = in.flags;
	backing->bytes = bytes;
	status = gpu_bo_fill(client, bo, &in, bytes);
	if (status != B_OK) {
		bo->backing = NULL;
		gpu_backing_drop(backing);
		return status;
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


/* The memory of a new backing. A refusal leaves nothing allocated. */
static status_t
gpu_bo_fill(gpu_client* client, gpu_bo* bo, r5_gpu_bo* request, uint64 bytes)
{
	r5_gpu_bo& in = *request;
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
					&& sBo[i].backing->domain == R5_GPU_DOMAIN_VRAM)
					held += sBo[i].backing->bytes;
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
		bo->backing->vramOffset = offset;
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
		bo->backing->kernelArea = area;
		bo->backing->kernel = kernel;
	}
	/* The reply carries the flags that were used. Write-combined GTT
	 * needs gfx9_wc_gtt. Otherwise the page stays cached and snooped. */
	{
		uint32 used = in.flags;
		if ((used & R5_GPU_BO_WC) != 0) {
			int allow = in.domain == R5_GPU_DOMAIN_GTT
				&& gR5GpuOps.gtt_wc != NULL && gR5GpuOps.gtt_wc() != 0;
			if (!allow || vm_set_area_page_memory_type(bo->backing->kernelArea,
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


/* Called with the GPU lock, from every path except the retire thread
 * that runs the reset. Returns with the lock once no reset runs. The
 * end of a reset (REOPEN or OFF) notifies sEop. */
static void
gpu_wait_reset()
{
	while (sResetting) {
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		gpu_unlock();
		entry.Wait(B_RELATIVE_TIMEOUT, 100000);
		gpu_lock();
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


/* The actions of r5_hang_step, in order. LOG and LOSE were done by the
 * caller, in the retire thread. sResetting keeps every other path that
 * may reach the chip out (gpu_wait_reset), so the lock is dropped
 * while the reset and the restart run; the self-test drops it while it
 * polls. */
static void
gpu_hang_run(uint32 actions)
{
	while (actions != 0) {
		uint32 next = 0;
		if ((actions & R5_HANG_ACT_RESET) != 0) {
			sResetting = true;
			gpu_unlock();
			bool ok = gR5GpuOps.reset_mode2 != NULL
				&& gR5GpuOps.reset_mode2(sInfo) == B_OK;
			gpu_lock();
			dprintf("radeon_hd: gpu reset mode2 %s\n", ok ? "ok" : "failed");
			sCpStopped = ok;
			next = r5_hang_step(&sHang, ok ? R5_HANG_EV_RESET_OK
				: R5_HANG_EV_RESET_FAILED, &sHang);
		} else if ((actions & R5_HANG_ACT_SELFTEST) != 0) {
			gpu_unlock();
			bool restarted = gR5GpuOps.restart != NULL
				&& gR5GpuOps.restart(sInfo) == B_OK;
			gpu_lock();
			bool ok = restarted;
			if (ok) {
				/* The CP is stopped and the hub is programmed again,
				 * so the page-table writes of a release reach it. */
				gpu_release_parked(~0ull);
				/* Every job of before is gone. The fence now reads the
				 * last sequence (restart), so nothing parked is left. */
				for (uint32 i = 0; i < kMaxJob; i++)
					sJobs[i].live = false;
				sLost = false;
				ok = gR5GpuOps.selftest != NULL
					&& gR5GpuOps.selftest(sInfo) == B_OK && !sLost;
			}
			/* A restart that failed is part of the reset; no self-test
			 * ran, so there is no self-test line. */
			if (!restarted) {
				next = r5_hang_step(&sHang, R5_HANG_EV_RESET_FAILED, &sHang);
			} else {
				dprintf("radeon_hd: gpu selftest after reset %s\n",
					ok ? "ok" : "failed");
				next = r5_hang_step(&sHang, ok ? R5_HANG_EV_SELFTEST_OK
					: R5_HANG_EV_SELFTEST_FAILED, &sHang);
			}
		} else if ((actions & R5_HANG_ACT_OFF) != 0) {
			sGpuOff = true;
			gpu_lost(sInfo, "gpu off");
			/* Without a reset that worked (a second hang, a failed
			 * reset) the CP may still reach parked buffers, so they
			 * stay allocated until reboot. */
			if (sCpStopped)
				gpu_release_parked(~0ull);
			if (gR5GpuOps.hang_release != NULL)
				gR5GpuOps.hang_release();
			sResetting = false;
			dprintf("radeon_hd: gpu off until reboot: %s\n",
				r5_hang_why(sHang.why));
			sEop.NotifyAll();
		} else if ((actions & R5_HANG_ACT_REOPEN) != 0) {
			/* Owners stay, so a lost file that frees its tree later
			 * releases only its own slots; every slot flushes on its
			 * next use. */
			if (sFileVmidsReady)
				r5_vmid_after_reset(&sFileVmids);
			/* The guilty file keeps its own flag while it is lost. */
			sGuilty = NULL;
			sLost = false;
			sResetting = false;
			sEop.NotifyAll();
		}
		actions = next;
	}
}


static void
gpu_hang(gpu_job* job, bigtime_t now)
{
	dprintf("radeon_hd: gpu hang ring gfx seq %llu after %lld ms\n",
		(unsigned long long)job->sequence,
		(long long)((now - job->submitted) / 1000));
	uint32 actions = r5_hang_step(&sHang, R5_HANG_EV_HANG, &sHang);
	sCpStopped = false;
	if ((actions & R5_HANG_ACT_LOSE) != 0) {
		if (sGuilty == NULL)
			sGuilty = job->owner;
		/* Per file, so the file of a second hang is guilty as well. */
		if (job->owner != NULL)
			job->owner->guilty = true;
		for (gpu_client* scan = sClientList; scan != NULL; scan = scan->next) {
			if (scan->contexts > 0)
				scan->lost = true;
		}
		gpu_lost(sInfo, "gpu lost watchdog");
	}
	gpu_hang_run(actions & ~(R5_HANG_ACT_LOG | R5_HANG_ACT_LOSE));
}


static void
gpu_watchdog()
{
	/* M11 step 3b: exports not imported within five seconds go. */
	if (!sResetting)
		gpu_share_purge(system_time());
	if (sLost || sGpuOff || sResetting || sInfo == NULL)
		return;
	bigtime_t now = system_time();
	uint64 seen = gpu_hw_seen();
	gpu_job* oldest = NULL;
	for (uint32 i = 0; i < kMaxJob; i++) {
		gpu_job* job = &sJobs[i];
		if (!job->live || r5_fence_reached(seen, job->sequence)
			|| job->submitted == 0)
			continue;
		if (oldest == NULL || job->submitted < oldest->submitted)
			oldest = job;
	}
	if (oldest == NULL || !r5_job_hung(seen, oldest->sequence,
			now - oldest->submitted, kWatchdogUs))
		return;
	gpu_hang(oldest, now);
}


/* gpu_hang_test <n>: armed by the last call of the sign-in self-test.
 * One job at a time; the next one only after the GPU came back. */
static void
gpu_hang_test_step()
{
	if (sHangTest == 0 || !sHangTestArmed || sHangTestJobs >= sHangTest
		|| sHangTestJobs > sHang.hangs)
		return;
	if (sLost || sGpuOff || sResetting || sInfo == NULL
		|| sHang.state != R5_HANG_RUNNING || gR5GpuOps.hang_job == NULL)
		return;
	uint64 sequence = 0;
	status_t status = gR5GpuOps.hang_job(sInfo, &sequence);
	if (status != B_OK) {
		dprintf("radeon_hd: gpu hang test refused %ld\n", (long)status);
		sHangTest = 0;
		return;
	}
	sHangTestJobs++;
	dprintf("radeon_hd: gpu hang test job %lu of %lu seq %llu\n",
		(unsigned long)sHangTestJobs, (unsigned long)sHangTest,
		(unsigned long long)sequence);
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
	if ((in.bo_offset & 0xfffull) != 0 || in.bo_offset > bo->backing->bytes
		|| in.map_bytes > bo->backing->bytes - in.bo_offset)
		return gpu_refuse("VA_MAP", "offset", B_BAD_VALUE, in.handle, bo->backing->domain,
			bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	for (uint32 i = 0; i < kMaxRange; i++) {
		gpu_range* range = &bo->ranges[i];
		if (!range->live)
			continue;
		if (r5_range_overlap(in.bo_offset, in.map_bytes, range->offset,
				range->bytes)
			|| r5_range_overlap(in.gpu_va, in.map_bytes, range->va,
				range->bytes))
			return gpu_refuse("VA_MAP", "overlap", B_BAD_VALUE, in.handle,
				bo->backing->domain, bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
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
		return gpu_refuse("VA_MAP", "ranges", B_NO_MEMORY, in.handle, bo->backing->domain,
			bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (gR5GpuOps.vmid_prepare == NULL)
		return gpu_refuse("VA_MAP", "vmid", B_NOT_SUPPORTED, in.handle,
			bo->backing->domain, bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
			in.flags);
	if (!gpu_hw_prepare())
		return gpu_refuse("VA_MAP", "vmid", B_NO_INIT, in.handle, bo->backing->domain,
			bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	uint32 flags = in.flags;
	if (flags == 0)
		flags = R5_GPU_VA_READ | R5_GPU_VA_WRITE;
	if (bo->backing->domain == R5_GPU_DOMAIN_VRAM) {
		if (gR5GpuOps.vram_phys == NULL || gR5GpuOps.encode_pte == NULL)
			return gpu_refuse("VA_MAP", "pte", B_NOT_SUPPORTED, in.handle,
				bo->backing->domain, bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
				in.flags);
		uint64 phys = 0;
		if (gR5GpuOps.vram_phys(client->info,
				bo->backing->vramOffset + in.bo_offset, &phys) != B_OK)
			return gpu_refuse("VA_MAP", "pte", B_NOT_SUPPORTED, in.handle,
				bo->backing->domain, bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
				in.flags);
		uint64 pte = gR5GpuOps.encode_pte(phys, flags, 0);
		for (uint64 page = 0; page < in.map_bytes; page += 4096) {
			if (gpu_hw_lookup(gpu_vm(client), in.gpu_va + page)) {
				gpu_va_clear(gpu_vm(client), in.gpu_va, page);
				return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE, in.handle,
					bo->backing->domain, bo->backing->bytes, in.gpu_va + page, in.bo_offset,
					in.map_bytes, in.flags);
			}
			if (!gpu_hw_map(gpu_vm(client), in.gpu_va + page, pte)) {
				dprintf("radeon_hd: page table pool full\n");
				gpu_va_clear(gpu_vm(client), in.gpu_va, page);
				return gpu_refuse("VA_MAP", "pte", B_NO_MEMORY, in.handle,
					bo->backing->domain, bo->backing->bytes, in.gpu_va + page, in.bo_offset,
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
		if (bo->backing->kernel == NULL)
			return gpu_refuse("VA_MAP", "gtt", B_NO_INIT, in.handle, bo->backing->domain,
				bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		uint8* cursor = (uint8*)bo->backing->kernel + in.bo_offset;
		uint64 filled = 0;
		while (filled < in.map_bytes) {
			physical_entry entries[256];
			uint32 count = 256;
			status_t mapped = get_memory_map_etc(B_CURRENT_TEAM, cursor,
				(size_t)(in.map_bytes - filled), entries, &count);
			if (mapped != B_OK && mapped != B_BUFFER_OVERFLOW) {
				gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
				return gpu_refuse("VA_MAP", "phys", mapped, in.handle,
					bo->backing->domain, bo->backing->bytes, in.gpu_va, in.bo_offset, filled,
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
							in.handle, bo->backing->domain, bo->backing->bytes,
							in.gpu_va + filled, in.bo_offset, in.map_bytes,
							in.flags);
					}
					if (gR5GpuOps.encode_pte == NULL)
						return gpu_refuse("VA_MAP", "pte", B_NOT_SUPPORTED,
							in.handle, bo->backing->domain, bo->backing->bytes,
							in.gpu_va + filled, in.bo_offset, in.map_bytes,
							in.flags);
					uint64 pte = gR5GpuOps.encode_pte(address, flags, 1);
					if (!gpu_hw_map(gpu_vm(client), in.gpu_va + filled, pte)) {
						dprintf("radeon_hd: page table pool full\n");
						gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
						return gpu_refuse("VA_MAP", "pte", B_NO_MEMORY,
							in.handle, bo->backing->domain, bo->backing->bytes,
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
				bo->backing->domain, bo->backing->bytes, in.gpu_va, in.bo_offset, filled,
				in.flags);
		}
	}
	if (gR5GpuOps.vm_flush == NULL || gR5GpuOps.vm_map == NULL)
		return gpu_refuse("VA_MAP", "flush", B_NOT_SUPPORTED, in.handle,
			bo->backing->domain, bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
			in.flags);
	if (!gpu_hw_flush()) {
		gpu_va_clear(gpu_vm(client), in.gpu_va, in.map_bytes);
		return gpu_refuse("VA_MAP", "flush", B_ERROR, in.handle, bo->backing->domain,
			bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
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
			(unsigned long)bo->backing->domain);
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
					bo->backing->domain, bo->backing->bytes, range->va + page, in.bo_offset,
					in.map_bytes, in.flags);
		}
		if (!gpu_hw_flush())
			return gpu_refuse("VA_UNMAP", "flush", B_ERROR, in.handle, bo->backing->domain,
				bo->backing->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		range->live = false;
		range->vm = NULL;
		return B_OK;
	}
	return gpu_refuse("VA_UNMAP", "range", B_BAD_VALUE, in.handle,
		bo != NULL ? bo->backing->domain : 0, bo != NULL ? bo->backing->bytes : 0, in.gpu_va,
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
	if (gR5GpuOps.ring_waited != NULL)
		gR5GpuOps.ring_waited();
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
		gpu_wait_reset();
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
	if (gpu_gone(client))
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
			if (gpu_gone(client))
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
			gpu_wait_reset();
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
	/* The room wait drops the lock. A hang in that time lost this
	 * context, or a reset is still running its self-test. */
	if (gpu_gone(client) || sResetting)
		return gpu_refuse("CS_SUBMIT", "lost", ECANCELED, in.context, 0,
			in.ib_count, 0, 0, in.ring, 0);
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
			if (bo == NULL || bo->backing->domain != R5_GPU_DOMAIN_GTT
				|| bo->backing->kernel == NULL)
				continue;
			for (uint32 r = 0; r < kMaxRange; r++) {
				gpu_range* range = &bo->ranges[r];
				if (!range->live)
					continue;
				if (ibs[i].gpu_va < range->va
					|| ibs[i].gpu_va + bytes > range->va + range->bytes)
					continue;
				uint8* cpu = (uint8*)bo->backing->kernel + range->offset
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
	gR5GpuOps.set_debug(gpu_submit_log_enabled() ? 1 : 0);
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
gpu_fence_wait(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_fence in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	status = gpu_wait_value(client, NULL, in.sequence, in.timeout_us);
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
		status = gpu_wait_value(client, &sync->point, sync->submitted,
			in.timeout_us);
	} else
		status = gpu_wait_value(client, &sync->point, in.point,
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
		if (gpu_gone(client))
			return ECANCELED;
		if (in.timeout_us == 0 || now >= deadline)
			return B_TIMED_OUT;
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		gpu_unlock();
		status_t waited = entry.Wait(B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT,
			deadline - now);
		gpu_lock();
		gpu_wait_reset();
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
	if (!gpu_src_reached(src, in.src_point) && gpu_gone(client))
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


static int sRingLogged = 0;


static status_t
gpu_ring_fill(void* buffer, size_t length)
{
	r5_gpu_ring_fill out;
	status_t status = gpu_copy_in(buffer, length, &out, sizeof(out));
	if (status != B_OK)
		return status;
	if (out.version != 1)
		return B_BAD_VALUE;
	out.waits = 0;
	out.max_fill = 0;
	out.dwords = 1024;
	if (gR5GpuOps.ring_fill != NULL)
		gR5GpuOps.ring_fill(&out.waits, &out.max_fill, &out.dwords);
	if (!sRingLogged) {
		sRingLogged = 1;
		dprintf("radeon_hd: ring waits %lu max %lu of %lu\n",
			(unsigned long)out.waits, (unsigned long)out.max_fill,
			(unsigned long)out.dwords);
	}
	return gpu_copy_out(buffer, length, &out, sizeof(out));
}


void r5_gfx9_ih_counts(uint32* msi, uint32* early, uint32* eop);


static status_t
gpu_ih_counts(void* buffer, size_t length)
{
	r5_gpu_ih_counts out;
	status_t status = gpu_copy_in(buffer, length, &out, sizeof(out));
	if (status != B_OK)
		return status;
	if (out.version != 1)
		return B_BAD_VALUE;
	r5_gfx9_ih_counts(&out.msi, &out.early, &out.eop);
	out.wake = r5_gpu_retire_wakes();
	return gpu_copy_out(buffer, length, &out, sizeof(out));
}


/* The path is the one load_image stored in the team info. It is not a
 * value from the ioctl. Roster resolves /system to /boot/system. */
static bool
gpu_caller_is_app_server()
{
	team_info info;
	if (_get_team_info(B_CURRENT_TEAM, &info, sizeof(info)) != B_OK)
		return false;
	static const char* kPaths[] = {
		"/system/servers/app_server",
		"/boot/system/servers/app_server"
	};
	for (uint32 i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); i++) {
		size_t length = strlen(kPaths[i]);
		if (strncmp(info.args, kPaths[i], length) == 0
			&& (info.args[length] == '\0' || info.args[length] == ' '))
			return true;
	}
	return false;
}


static status_t
gpu_scanout(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_scanout in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	if (in.version != 1)
		return B_BAD_VALUE;
	if (!gpu_caller_is_app_server())
		return B_NOT_ALLOWED;
	if (gR5GpuOps.scanout == NULL)
		return B_NOT_SUPPORTED;
	uint32 width = 0;
	uint32 height = 0;
	uint32 pitch = 0;
	uint32 format = 0;
	uint64 offset = 0;
	uint64 bytes = 0;
	if (gR5GpuOps.scanout(client->info, &width, &height, &pitch, &format,
			&offset, &bytes) != B_OK)
		return B_ERROR;
	int reason = R5_SHARE_OK;
	int slot = gpu_slot_take(client, &reason);
	if (slot < 0)
		return B_NO_MEMORY;
	gpu_backing* backing = gpu_backing_take();
	if (backing == NULL)
		return B_NO_MEMORY;
	gpu_bo* bo = &sBo[slot];
	memset(bo, 0, sizeof(*bo));
	bo->area = -1;
	bo->mapTeam = -1;
	bo->backing = backing;
	backing->domain = R5_GPU_DOMAIN_VRAM;
	backing->bytes = bytes;
	backing->vramOffset = offset;
	backing->borrowed = true;
	bo->live = true;
	bo->owner = client;

	uint64 va = 0x100000000ull;
	bool freeVa = false;
	for (int n = 0; n < 16; n++) {
		if (gpu_va_fits(va, bytes) && !gpu_hw_lookup(gpu_vm(client), va)) {
			freeVa = true;
			break;
		}
		va += 0x4000000ull;
	}
	if (!freeVa) {
		gpu_bo_release(bo);
		return B_NO_MEMORY;
	}
	r5_gpu_va map;
	memset(&map, 0, sizeof(map));
	map.size = sizeof(map);
	map.version = 1;
	map.context = 1;
	map.handle = (uint32)slot + 1u;
	map.gpu_va = va;
	map.map_bytes = bytes;
	map.flags = R5_GPU_VA_READ | R5_GPU_VA_WRITE;
	status = gpu_va_map_copied(client, &map);
	if (status != B_OK) {
		gpu_bo_release(bo);
		return status;
	}
	client->handles[client->handleCount++] = map.handle;
	in.handle = map.handle;
	in.width = width;
	in.height = height;
	in.bytes_per_row = pitch;
	in.format = format;
	in.gpu_va = va;
	in.bytes = bytes;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK) {
		client->handleCount--;
		gpu_bo_release(bo);
	}
	return status;
}


/* The Haiku status of a refusal (r5_share_error, host-tested). */
static status_t
gpu_share_status(int reason)
{
	switch (r5_share_error(reason)) {
		case R5_SHARE_E_OK:
			return B_OK;
		case R5_SHARE_E_NOT_ALLOWED:
			return B_NOT_ALLOWED;
		case R5_SHARE_E_NO_MEMORY:
			return B_NO_MEMORY;
		case R5_SHARE_E_CANCELED:
			return ECANCELED;
	}
	return B_BAD_VALUE;
}


/* M11 step 3b (docs/APP-SERVER-COMPOSITOR.md, "Zero-copy design").
 * Removes one pending export and the reference it holds. */
static void
gpu_share_remove(r5_share_export* record)
{
	if (!record->live)
		return;
	record->live = 0;
	if (record->backing < kMaxBo)
		gpu_backing_drop(&sBacking[record->backing]);
	memset(record, 0, sizeof(*record));
}


/* Removes every pending export older than five seconds. Called at each
 * export and import and from the watchdog, with the GPU lock. */
static void
gpu_share_purge(bigtime_t now)
{
	for (gpu_client* scan = sClientList; scan != NULL; scan = scan->next) {
		for (uint32 i = 0; i < R5_SHARE_EXPORTS_PER_FILE; i++) {
			r5_share_export* record = &scan->exports[i];
			if (record->live && r5_share_expired(now, record->made))
				gpu_share_remove(record);
		}
	}
}


/* R5_GPU_BO_EXPORT (+ 17). Any file may export its own buffer to the
 * compositor. The calling team is pinned in the record. */
static status_t
gpu_bo_export(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo_export in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_EXPORT", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.version != 1)
		return gpu_refuse("BO_EXPORT", "version", B_BAD_VALUE, in.handle, 0, 0,
			0, 0, 0, in.flags);
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return gpu_refuse("BO_EXPORT", "bo", B_BAD_VALUE, in.handle, 0, 0, 0,
			0, 0, in.flags);
	bigtime_t now = system_time();
	gpu_share_purge(now);
	uint32 pending = r5_share_pending(client->exports,
		R5_SHARE_EXPORTS_PER_FILE);
	int reason = r5_share_check_export(in.target, R5_GPU_SHARE_COMPOSITOR,
		in.target_team, in.flags, pending);
	if (reason != R5_SHARE_OK) {
		return gpu_refuse("BO_EXPORT", r5_share_reason(reason),
			gpu_share_status(reason), in.handle, bo->backing->domain,
			bo->backing->bytes, 0, 0, 0, in.flags);
	}
	r5_share_export* record = NULL;
	for (uint32 i = 0; i < R5_SHARE_EXPORTS_PER_FILE; i++) {
		if (!client->exports[i].live) {
			record = &client->exports[i];
			break;
		}
	}
	if (record == NULL)
		return B_NO_MEMORY;
	sShareCounter++;
	memset(record, 0, sizeof(*record));
	r5_share_make_token(sShareCounter, secure_random_value(),
		secure_random_value(), record->token);
	record->backing = (uint32)(bo->backing - sBacking);
	record->team = team_get_current_team_id();
	record->made = now;
	record->target = in.target;
	record->live = 1;
	r5_share_ref_hold(&bo->backing->refs);
	in.bytes = bo->backing->bytes;
	memcpy(in.token, record->token, sizeof(in.token));
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		gpu_share_remove(record);
	return status;
}


/* R5_GPU_BO_IMPORT (+ 18). Only app_server, for the team it names, once,
 * within five seconds. A refused number stays where it was: a forwarded
 * number does not take the window of the team that exported it. */
static status_t
gpu_bo_import(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo_import in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_IMPORT", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.version != 1)
		return gpu_refuse("BO_IMPORT", "version", B_BAD_VALUE, 0, 0, 0, 0, 0,
			0, in.flags);
	int appServer = gpu_caller_is_app_server() ? 1 : 0;
	bigtime_t now = system_time();
	r5_share_export* record = NULL;
	gpu_client* exporter = NULL;
	if (appServer) {
		/* The match is read before the purge, so a number that just
		 * expired says "expired", not "token". */
		for (gpu_client* scan = sClientList; scan != NULL && record == NULL;
				scan = scan->next) {
			int found = r5_share_find(scan->exports,
				R5_SHARE_EXPORTS_PER_FILE, in.token);
			if (found >= 0) {
				record = &scan->exports[found];
				exporter = scan;
			}
		}
	}
	int reason = r5_share_check_import(appServer, in.flags, record,
		in.client_team, now, exporter != NULL && gpu_gone(exporter));
	if (reason == R5_SHARE_EXPIRED)
		gpu_share_remove(record);
	gpu_share_purge(now);
	if (reason != R5_SHARE_OK) {
		return gpu_refuse("BO_IMPORT", r5_share_reason(reason),
			gpu_share_status(reason), 0, 0, 0, 0, 0, 0, in.flags);
	}
	int slotReason = R5_SHARE_OK;
	int slot = gpu_slot_take(client, &slotReason);
	if (slot < 0 || record->backing >= kMaxBo) {
		return gpu_refuse("BO_IMPORT", r5_share_reason(slotReason),
			B_NO_MEMORY, 0, 0, 0, 0, 0, 0, in.flags);
	}
	gpu_backing* backing = &sBacking[record->backing];
	/* The record's reference becomes the reference of the new slot. */
	record->live = 0;
	memset(record, 0, sizeof(*record));
	gpu_bo* bo = &sBo[slot];
	memset(bo, 0, sizeof(*bo));
	bo->area = -1;
	bo->mapTeam = -1;
	bo->backing = backing;
	bo->live = true;
	bo->owner = client;
	client->handles[client->handleCount++] = (uint32)slot + 1u;
	in.handle = (uint32)slot + 1u;
	in.domain = backing->domain;
	in.bo_flags = backing->flags;
	in._pad = 0;
	in.bytes = backing->bytes;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		gpu_bo_detach(client, bo, in.handle);
	return status;
}


/* R5_GPU_BO_PRESENT_MARK (+ 32). SET records the calling file's last
 * submit on its own buffer; the program cannot give a number. GET reads
 * it, for app_server's FENCE_WAIT with timeout 0. */
static status_t
gpu_bo_mark(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo_mark in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	if (in.version != 1)
		return B_BAD_VALUE;
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return B_BAD_VALUE;
	if (in.op == R5_GPU_MARK_SET)
		bo->backing->readySequence = client->lastSequence;
	else if (in.op != R5_GPU_MARK_GET)
		return B_BAD_VALUE;
	in.sequence = bo->backing->readySequence;
	return gpu_copy_out(buffer, length, &in, sizeof(in));
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
	if (op == R5_GPU_RING_FILL)
		return gpu_ring_fill(buffer, length);
	if (op == R5_GPU_IH_COUNTS) {
		/* The last call of r5-gpu-selftest. gpu_hang_test starts after
		 * it, from the retire thread. */
		gpu_lock();
		if (sHangTest != 0 && !sHangTestArmed) {
			sHangTestArmed = true;
			sInfo = client->info;
			dprintf("radeon_hd: gpu hang test armed %lu\n",
				(unsigned long)sHangTest);
			r5_gpu_retire_wake();
		}
		gpu_unlock();
		return gpu_ih_counts(buffer, length);
	}
	if (!gpu_render_enabled())
		return B_NOT_ALLOWED;

	gpu_lock();
	gpu_wait_reset();
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
		else if (sGpuOff || client->lost)
			status = ECANCELED;
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
			/* The lost context is gone. A new one on this file works. */
			if (client->contexts == 0 && client->lost) {
				client->lost = false;
				client->guilty = false;
				if (sGuilty == client)
					sGuilty = NULL;
			}
			status = B_OK;
		}
	} else if (op == R5_GPU_BO_CREATE)
		status = gpu_bo_create(client, buffer, length);
	else if (op == R5_GPU_SCANOUT_BO)
		status = gpu_scanout(client, buffer, length);
	else if (op == R5_GPU_BO_EXPORT)
		status = gpu_bo_export(client, buffer, length);
	else if (op == R5_GPU_BO_IMPORT)
		status = gpu_bo_import(client, buffer, length);
	else if (op == R5_GPU_BO_PRESENT_MARK)
		status = gpu_bo_mark(client, buffer, length);
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
		else if (gpu_gone(client)) {
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
		if (gpu_gone(client)) {
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
		status = gpu_fence_wait(client, buffer, length);
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
gpu_wait_value(gpu_client* client, volatile uint64* value, uint64 target,
	uint64 timeoutUs)
{
	/* A lost context gets no result, also not for a job that the reset
	 * counted as retired. */
	if (client != NULL && client->lost)
		return ECANCELED;
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
		if (gpu_gone(client))
			return ECANCELED;
		if (step == R5_WAIT_TIMEOUT)
			return B_TIMED_OUT;
		bigtime_t left = deadline - now;
		gpu_unlock();
		entry.Wait(B_RELATIVE_TIMEOUT, left);
		gpu_lock();
		gpu_wait_reset();
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
		/* AFTER-M8 item 11: the fixed line, with the hub status that
		 * gmc_v9_0_process_interrupt reads and clears. At most one
		 * status line per second; the M5 pasid lines stay one per
		 * fault, because check-user reads them per VMID. The status is
		 * read and cleared once per line; it latches the first fault
		 * since the last clear. */
		bigtime_t now = system_time();
		bool line = sFaultLineAt == 0 || now - sFaultLineAt >= 1000000;
		if (line) {
			uint32 status = 0;
			if (gR5GpuOps.fault_status != NULL)
				gR5GpuOps.fault_status(&status);
			sFaultLineAt = now;
			dprintf("radeon_hd: vm fault vmid %lu addr %016llx status %08lx\n",
				(unsigned long)vmid, (unsigned long long)addr,
				(unsigned long)status);
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
				bo->backing->domain, bo->backing->bytes, 0, bo->backing->vramOffset, 0, bo->backing->flags);
		in.area = bo->area;
		return gpu_copy_out(buffer, length, &in, sizeof(in));
	}
	/* M11 step 3b: the kernel area belongs to the backing. A second
	 * slot of the same backing (an import) clones the one that exists. */
	if (bo->backing->domain == R5_GPU_DOMAIN_VRAM
		&& bo->backing->kernelArea < 0) {
		void* kernel = NULL;
		if (gR5GpuOps.map_vram == NULL)
			return gpu_refuse("BO_MAP", "vram", B_NOT_SUPPORTED, in.handle,
				bo->backing->domain, bo->backing->bytes, 0, bo->backing->vramOffset, 0, bo->backing->flags);
		area_id kernelArea = gR5GpuOps.map_vram(bo->backing->vramOffset, bo->backing->bytes,
			&kernel);
		if (kernelArea < 0 || kernel == NULL)
			return gpu_refuse("BO_MAP", "vram", kernelArea, in.handle, bo->backing->domain,
				bo->backing->bytes, 0, bo->backing->vramOffset, 0, bo->backing->flags);
		bo->backing->kernelArea = kernelArea;
		bo->backing->kernel = kernel;
	}
	void* user = NULL;
	area_id area = vm_clone_area(team, "r5 gpu bo", &user, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, kRegionNoPrivateMap, bo->backing->kernelArea,
		true);
	if (area < 0)
		return gpu_refuse("BO_MAP", "clone", area, in.handle, bo->backing->domain,
			bo->backing->bytes, 0, bo->backing->vramOffset, 0, bo->backing->flags);
	/* vm_clone_area() copies the memory type for device areas only.
	 * A GTT buffer is RAM, so the program's mapping stays write-back
	 * until this call. */
	if ((bo->backing->flags & R5_GPU_BO_WC) != 0
		&& vm_set_area_page_memory_type(area, B_WRITE_COMBINING_MEMORY)
			!= B_OK) {
		vm_delete_area(team, area, true);
		return gpu_refuse("BO_MAP", "wc", B_NOT_SUPPORTED, in.handle,
			bo->backing->domain, bo->backing->bytes, 0, bo->backing->vramOffset, 0, bo->backing->flags);
	}
	bo->area = area;
	bo->mapTeam = team;
	in.area = area;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_MAP", "copy", status, in.handle, bo->backing->domain,
			bo->backing->bytes, 0, bo->backing->vramOffset, 0, bo->backing->flags);
	return B_OK;
}


static status_t
gpu_free(void* cookie)
{
	gpu_client* client = (gpu_client*)cookie;
	if (client == NULL)
		return B_OK;
	gpu_lock();
	/* Freeing buffers writes page tables and flushes the TLB. */
	gpu_wait_reset();
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
		/* The watchdog reads the owner of a job that hangs later. */
		if (sJobs[i].owner == client)
			sJobs[i].owner = NULL;
	}
	/* M11 step 3b: exports not imported yet go with their file. */
	for (uint32 i = 0; i < R5_SHARE_EXPORTS_PER_FILE; i++)
		gpu_share_remove(&client->exports[i]);
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
	if (sGuilty == client)
		sGuilty = NULL;
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
