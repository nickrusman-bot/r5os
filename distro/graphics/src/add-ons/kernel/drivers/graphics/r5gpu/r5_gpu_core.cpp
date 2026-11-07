/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Vendor-neutral GPU core. User memory is copied only here. AMD
 * registers, page tables, rings and interrupts stay in r5_gpu.cpp.
 */

#include "r5_gpu_internal.h"
#include "r5_vm.h"
#include "radeon_hd_private.h"
#include "r5_gpu.h"
#include "r5_span.h"
#include "r5_wait.h"
#include "r5_stage_p.h"
#include "r5_log.h"

#include <KernelExport.h>
#include <kernel.h>

#include <driver_settings.h>
#include <stdlib.h>
#include <string.h>


/* vm.h pulls thread types in before the mutex type. These are the calls
 * radeon_hd already resolves from the kernel, same as vm_set_area_memory_type.
 */
extern "C" area_id vm_clone_area(team_id team, const char* name, void** address,
	uint32 addressSpec, uint32 protection, uint32 mapping, area_id sourceArea,
	bool kernel);
extern "C" status_t vm_delete_area(team_id teamID, area_id areaID, bool kernel);
extern "C" team_id team_get_current_team_id(void);
extern "C" bool team_is_valid(team_id id);

extern radeon_info* gDeviceInfo[];

/* headers/private/system/vm_defs.h: REGION_NO_PRIVATE_MAP = 0. */
static const uint32 kRegionNoPrivateMap = 0;


gpu_bo sBo[kMaxBo];
gpu_job sJobs[kMaxJob];
gpu_prt sPrt[kMaxPrt];
uint64 sHighestSequence;
uint64 sLastIbVa;
uint64 sLastIbList[8];
uint32 sLastIbDw[8];
uint32 sLastIbN;
radeon_info* sInfo;
bool sReplacing;
lock sGpuLock;
ConditionVariable sEop;
bool sReady = false;
bool sLost = false;
static void* sGuiltyAs = NULL;
int32 sContexts = 0;
sem_id sRetireSem = -1;
thread_id sRetireThread = -1;
int sErrorLines;
int sOnceBits;


static status_t
gpu_vram_base(void* info, uint32* base)
{
	if (gR5GpuOps.read_reg == NULL || base == NULL)
		return B_NOT_SUPPORTED;
	return gR5GpuOps.read_reg(info, R5_GPU_QUERY_VRAM_BASE, base);
}


static uint64
gpu_seen()
{
	uint64 seen = 0;
	if (gR5GpuOps.poll_retired == NULL
		|| gR5GpuOps.poll_retired(&seen) != B_OK)
		return 0;
	return seen;
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


status_t
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


bool
gpu_debug_enabled()
{
	bool enabled = false;
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		enabled = get_driver_boolean_parameter(settings, "gpu_debug",
			false, true);
		unload_driver_settings(settings);
	}
	return enabled;
}


bool
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
static status_t
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
	/* The node is open to every account once the GPU steps are on.
	 * gpu_render in the settings file is ignored. */
	return true;
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
gpu_lost(radeon_info* info, const char* why)
{
	if (sLost)
		return;
	sLost = true;
	(void)info;
	for (uint32 i = 0; i < kMaxJob; i++)
		sJobs[i].live = false;
	sEop.NotifyAll();
	if (gR5GpuOps.dump != NULL)
		gR5GpuOps.dump(why);
}




static void r5_gpu_eop_at(uint64 seen);
static void r5_gpu_eop();
static void gpu_sync_host(gpu_sync* sync);


static void gpu_release_parked(uint64 seen);
static void gpu_watchdog();


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
		gpu_release_parked(gpu_seen());
		if (gR5GpuOps.dump != NULL)
			gR5GpuOps.dump(NULL);
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


static void
r5_gpu_eop_at(uint64 seen)
{
	if (!sReady)
		return;
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
	/* Unmap parked buffers before a parked space can be freed. */
	gpu_release_parked(seen);
	if (gR5GpuOps.note_retired != NULL)
		gR5GpuOps.note_retired(seen);
}


static void
r5_gpu_eop()
{
	r5_gpu_eop_at(gpu_seen());
}


static bool
gpu_space_lost(gpu_client* client)
{
	if (sLost)
		return true;
	if (client == NULL || client->address == NULL
		|| gR5GpuOps.space_lost == NULL)
		return false;
	return gR5GpuOps.space_lost(client->address) != B_OK;
}


static uint64
gpu_counter(volatile uint64* value)
{
	if (value == NULL)
		return gpu_seen();
	return *value;
}


static status_t
gpu_wait_value(gpu_client* client, volatile uint64* value, uint64 target,
	uint64 timeoutUs)
{
	(void)client;
	int step = r5_wait_step(gpu_counter(value), target, timeoutUs, 0);
	if (step == R5_WAIT_DONE)
		return B_OK;
	if (gpu_space_lost(client))
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
		if (gpu_space_lost(client))
			return ECANCELED;
		if (step == R5_WAIT_TIMEOUT)
			return B_TIMED_OUT;
		bigtime_t left = deadline - now;
		gpu_unlock();
		entry.Wait(B_RELATIVE_TIMEOUT, left);
		gpu_lock();
	}
}


static status_t
gpu_get_info(gpu_client* client, radeon_info* info, void* buffer, size_t length)
{
	if (info == NULL || info->pci == NULL || buffer == NULL || length < 8)
		return B_BAD_VALUE;
	if (!is_user_address_range(buffer, length))
		return B_BAD_ADDRESS;
	uint32 claimed = 0;
	if (user_memcpy(&claimed, buffer, sizeof(claimed)) != B_OK)
		return B_BAD_ADDRESS;
	if (claimed > length)
		return B_BAD_VALUE;

	r5_gpu_info out;
	memset(&out, 0, sizeof(out));
	out.size = (uint32)sizeof(out);
	out.version = 1;
	out.api_major = R5_GPU_API_MAJOR;
	out.api_minor = R5_GPU_API_MINOR;
	/* Linux v6.12 PCI vendor ATI. */
	out.vendor = 0x1002;
	out.device = info->pci->device_id;
	out.revision = info->pci->revision;
	if (gpu_render_enabled())
		out.capabilities = R5_GPU_CAP_RENDER;
	bool fileLost = false;
	if (client != NULL && client->address != NULL
		&& gR5GpuOps.space_lost != NULL)
		fileLost = gR5GpuOps.space_lost(client->address) != B_OK;
	if (sLost || fileLost)
		out.capabilities |= R5_GPU_CAP_LOST;
	if (fileLost || (client != NULL && client->address != NULL
			&& client->address == sGuiltyAs))
		out.capabilities |= R5_GPU_CAP_GUILTY;
	out.va_offset = R5_GPU_VA_OFFSET;
	out.va_max = R5_GPU_VA_MAX;
	/* Linux v6.12 VM manager: the hole ends at the high range start
	 * and the high manager runs up to the byte size minus the reserved
	 * top. The page tables are the existing 4-level tables. */
	out.high_va_offset = R5_GPU_HIGH_VA_OFFSET;
	out.high_va_max = R5_GPU_HIGH_VA_MAX;
	out.vendor_block_size = 0;
	out.vendor_block_version = 0;
	if (gR5GpuOps.vendor_size != NULL)
		gR5GpuOps.vendor_size(&out.vendor_block_size, &out.vendor_block_version);
	out.bus = info->pci->bus;
	out.dev = info->pci->device;
	out.func = info->pci->function;
	out.subsystem_vendor = info->pci->u.h0.subsystem_vendor_id;
	out.subsystem_device = info->pci->u.h0.subsystem_id;
	if (info->shared_info != NULL) {
		out.vram_bytes = (uint64)info->shared_info->frame_buffer_size * 1024ull;
		out.gtt_bytes = out.vram_bytes;
	}
	for (uint32 i = 0; i < kMaxBo; i++) {
		if (!sBo[i].live)
			continue;
		if (sBo[i].domain == R5_GPU_DOMAIN_VRAM)
			out.vram_usage += sBo[i].bytes;
		else if (sBo[i].domain == R5_GPU_DOMAIN_GTT)
			out.gtt_usage += sBo[i].bytes;
	}
	/* The whole BAR is CPU-visible on this APU. There is no invisible heap. */
	out.visible_vram_usage = out.vram_usage;
	size_t copy = sizeof(out);
	if (length < copy)
		copy = length;
	if (user_memcpy(buffer, &out, copy) != B_OK)
		return B_BAD_ADDRESS;
	if (out.vendor_block_size == 0
		|| length < sizeof(out) + out.vendor_block_size)
		return B_OK;
	if (gR5GpuOps.fill_vendor_info == NULL)
		return B_NOT_SUPPORTED;
	uint8* block = (uint8*)malloc(out.vendor_block_size);
	if (block == NULL)
		return B_NO_MEMORY;
	memset(block, 0, out.vendor_block_size);
	{
		int filled = gR5GpuOps.fill_vendor_info(info, block,
			out.vendor_block_size);
		if (filled != B_OK) {
			free(block);
			return filled;
		}
	}
	status_t copied = user_memcpy((uint8*)buffer + sizeof(out), block,
		out.vendor_block_size);
	free(block);
	if (copied != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static void
gpu_bo_unmap(gpu_bo* bo)
{
	bool any = false;
	for (uint32 i = 0; i < kMaxRange; i++) {
		gpu_range* range = &bo->ranges[i];
		if (!range->live)
			continue;
		for (uint64 page = 0; page < range->bytes; page += 4096) {
			if (gR5GpuOps.vm_map(bo->space, range->va + page, 0) != B_OK)
				continue;
		}
		range->live = false;
		any = true;
	}
	if (any && gR5GpuOps.tlb_flush(bo->space) != B_OK)
		return;
}


static void
gpu_bo_release(gpu_bo* bo)
{
	if (!bo->live)
		return;
	gpu_bo_unmap(bo);
	if (bo->domain == R5_GPU_DOMAIN_VRAM)
		gR5GpuOps.vram_free(bo->vramOffset);
	/* The user clone belongs to mapTeam. A team that has already ended
	 * took its areas with it. team_is_valid is the check; do not delete
	 * an id in a team that is gone.
	 */
	if (bo->area >= 0 && team_is_valid(bo->mapTeam))
		vm_delete_area(bo->mapTeam, bo->area, true);
	if (bo->kernelArea >= 0)
		delete_area(bo->kernelArea);
	if (bo->owner != NULL) {
		uint64* used = bo->domain == R5_GPU_DOMAIN_VRAM
			? &bo->owner->vramUsed : &bo->owner->gttUsed;
		if (*used >= bo->bytes)
			*used -= bo->bytes;
		else
			*used = 0;
	}
	void* space = bo->space;
	int held = bo->parked;
	bo->live = false;
	bo->owner = NULL;
	bo->parked = false;
	bo->parkSequence = 0;
	bo->area = -1;
	bo->mapTeam = -1;
	bo->kernelArea = -1;
	bo->kernel = NULL;
	bo->space = NULL;
	if (held && space != NULL && gR5GpuOps.space_drop != NULL)
		gR5GpuOps.space_drop(space);
}


bool gpu_selftest_enabled();


static status_t
gpu_bo_create(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_CREATE", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.bytes == 0)
		return gpu_refuse("BO_CREATE", "size", B_BAD_VALUE, 0, in.domain,
			in.bytes, 0, 0, 0, in.flags);
	if (in.domain != R5_GPU_DOMAIN_VRAM && in.domain != R5_GPU_DOMAIN_GTT)
		return gpu_refuse("BO_CREATE", "domain", B_BAD_VALUE, 0, in.domain,
			in.bytes, 0, 0, 0, in.flags);
	if (client->handleCount >= kMaxClientBo)
		return gpu_refuse("BO_CREATE", "handles", B_NO_MEMORY, 0, in.domain,
			in.bytes, 0, 0, 0, in.flags);
	uint64 bytes = (in.bytes + 4095ull) & ~4095ull;
	uint64 vramCap = 0;
	uint64 gttCap = 0;
	if (gR5GpuOps.limits == NULL
		|| gR5GpuOps.limits(&vramCap, &gttCap) != B_OK)
		return gpu_refuse("BO_CREATE", "limit", B_NO_MEMORY, 0, in.domain,
			bytes, 0, 0, 0, in.flags);
	{
		uint64 used = in.domain == R5_GPU_DOMAIN_VRAM
			? client->vramUsed : client->gttUsed;
		uint64 cap = in.domain == R5_GPU_DOMAIN_VRAM ? vramCap : gttCap;
		if (cap == 0 || bytes > cap || used > cap - bytes)
			return gpu_refuse("BO_CREATE", "limit", B_NO_MEMORY, 0, in.domain,
				bytes, 0, 0, 0, in.flags);
	}
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
		/* R5_GPU_BO_WC is accepted and not applied. GTT stays cached. */
		bo->kernelArea = area;
		bo->kernel = kernel;
	}
	bo->live = true;
	bo->owner = client;
	bo->space = client->address;
	if (bo->domain == R5_GPU_DOMAIN_VRAM)
		client->vramUsed += bo->bytes;
	else
		client->gttUsed += bo->bytes;
	client->handles[client->handleCount++] = (uint32)slot + 1u;
	in.handle = (uint32)slot + 1u;
	in.area = bo->area;
	in.bytes = bytes;
	{
		uint32 requested = (uint32)in.flags;
		uint32 used = requested & ~R5_GPU_BO_WC;
		if (gpu_selftest_enabled())
			dprintf("radeon_hd: bo domain %lu requested %08lx used %08lx "
				"bytes %llu snooped %d\n",
				(unsigned long)in.domain, (unsigned long)requested,
				(unsigned long)used, (unsigned long long)bytes,
				in.domain == R5_GPU_DOMAIN_GTT ? 1 : 0);
		in.flags = used;
	}
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_CREATE", "copy", status, in.handle, in.domain,
			bytes, 0, 0, 0, in.flags);
	return B_OK;
}


static gpu_bo*
gpu_bo_get(gpu_client* client, uint32 handle)
{
	if (handle == 0 || handle > kMaxBo)
		return NULL;
	gpu_bo* bo = &sBo[handle - 1u];
	if (!bo->live || bo->owner != client)
		return NULL;
	return bo;
}


static void
gpu_release_parked(uint64 seen)
{
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
	uint64 seen = gpu_seen();
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
	if (oldest->owner != NULL && oldest->owner->address != NULL)
		sGuiltyAs = oldest->owner->address;
	gpu_lost(sInfo, "gpu lost watchdog");
	gpu_release_parked(~0ull);
	if (gR5GpuOps.note_retired != NULL)
		gR5GpuOps.note_retired(~0ull);
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
	if (!sLost && sequence != 0 && gpu_seen() < sequence) {
		if (bo->domain == R5_GPU_DOMAIN_VRAM) {
			if (client->vramUsed >= bo->bytes)
				client->vramUsed -= bo->bytes;
		} else if (client->gttUsed >= bo->bytes)
			client->gttUsed -= bo->bytes;
		bo->owner = NULL;
		bo->parked = true;
		bo->parkSequence = sequence;
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
		uint32 fb = 0;
		if (gpu_vram_base(client->info, &fb) != B_OK)
			return gpu_refuse("BO_MAP", "vram", B_NO_INIT, in.handle, bo->domain,
				bo->bytes, 0, bo->vramOffset, 0, bo->flags);
		phys_addr_t phys = (phys_addr_t)(((uint64)fb << 24) + bo->vramOffset);
		void* kernel = NULL;
		area_id kernelArea = map_physical_memory("r5 gpu vram", phys,
			(size_t)bo->bytes, B_ANY_KERNEL_ADDRESS,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
			&kernel);
		if (kernelArea < 0)
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


static bool
gpu_va_fits(uint64 va, uint64 bytes)
{
	/* Low range, or the high range. A hole address fits neither. */
	uint64 end;
	int low;
	int high;
	if (bytes == 0 || (va & 0xfffull) != 0 || (bytes & 0xfffull) != 0)
		return false;
	end = va + bytes;
	if (end < va)
		return false;
	low = va >= R5_GPU_VA_OFFSET && end <= R5_GPU_VA_MAX;
	high = va >= R5_GPU_HIGH_VA_OFFSET && end <= R5_GPU_HIGH_VA_MAX;
	return low || high;
}


static void
gpu_va_clear(void* space, uint64 va, uint64 bytes)
{
	for (uint64 page = 0; page < bytes; page += 4096) {
		if (gR5GpuOps.vm_map(space, va + page, 0) != B_OK)
			continue;
	}
	if (bytes != 0 && gR5GpuOps.tlb_flush(space) != B_OK)
		return;
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
gpu_bo_partial(void* space, uint64 va, uint64 bytes)
{
	for (uint32 b = 0; b < kMaxBo; b++) {
		if (!sBo[b].live || sBo[b].space != space)
			continue;
		for (uint32 i = 0; i < kMaxRange; i++) {
			gpu_range* range = &sBo[b].ranges[i];
			if (!range->live)
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
gpu_va_bo_overlap(void* space, uint64 va, uint64 bytes)
{
	for (uint32 b = 0; b < kMaxBo; b++) {
		if (!sBo[b].live || sBo[b].space != space)
			continue;
		for (uint32 i = 0; i < kMaxRange; i++) {
			gpu_range* range = &sBo[b].ranges[i];
			if (range->live && r5_range_overlap(va, bytes, range->va,
					range->bytes))
				return true;
		}
	}
	return false;
}


static bool
gpu_prt_overlaps(void* space, uint64 va, uint64 bytes)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live && sPrt[i].space == space
			&& r5_range_overlap(va, bytes, sPrt[i].va, sPrt[i].bytes))
			return true;
	}
	return false;
}


static bool
gpu_prt_covers(void* space, uint64 va, uint64 bytes)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (!sPrt[i].live || sPrt[i].space != space)
			continue;
		if (gpu_span_inside(va, bytes, sPrt[i].va, sPrt[i].bytes))
			return true;
	}
	return false;
}


static void
gpu_ranges_drop_contained(void* space, uint64 va, uint64 bytes)
{
	for (uint32 b = 0; b < kMaxBo; b++) {
		if (sBo[b].space != space)
			continue;
		for (uint32 i = 0; i < kMaxRange; i++) {
			gpu_range* range = &sBo[b].ranges[i];
			if (range->live && gpu_span_inside(range->va, range->bytes, va,
					bytes))
				range->live = false;
		}
	}
}


static void
gpu_prt_drop_contained(void* space, uint64 va, uint64 bytes)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live && sPrt[i].space == space
			&& gpu_span_inside(sPrt[i].va, sPrt[i].bytes, va, bytes))
			sPrt[i].live = false;
	}
}


static void
gpu_prt_release_space(void* space)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live && sPrt[i].space == space)
			sPrt[i].live = false;
	}
}


static status_t
gpu_prt_add(void* space, uint64 va, uint64 bytes)
{
	if (gpu_prt_covers(space, va, bytes))
		return B_OK;
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live)
			continue;
		sPrt[i].live = true;
		sPrt[i].space = space;
		sPrt[i].va = va;
		sPrt[i].bytes = bytes;
		return B_OK;
	}
	return B_NO_MEMORY;
}


static status_t
gpu_va_write_prt(void* space, uint64 va, uint64 bytes)
{
	/* The PTE is a value built here: bit 51, VALID clear, no address.
	 * The backend writes that page-table entry. This path does
	 * not read or write a user address (SMAP).
	 */
	/* One page at a time, so a failure clears only the pages just written. */
	for (uint64 page = 0; page < bytes; page += 4096) {
		if (gR5GpuOps.vm_prt == NULL
			|| gR5GpuOps.vm_prt(space, va + page, 4096) != B_OK) {
			gpu_va_clear(space, va, page);
			return gR5GpuOps.vm_prt == NULL ? B_NOT_SUPPORTED : B_NO_MEMORY;
		}
	}
	if (gR5GpuOps.tlb_flush == NULL || gR5GpuOps.tlb_flush(space) != B_OK) {
		gpu_va_clear(space, va, bytes);
		return gR5GpuOps.tlb_flush == NULL ? B_NOT_SUPPORTED : B_ERROR;
	}
	return B_OK;
}


static status_t
gpu_va_map(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_va in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("VA_MAP", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.context != 1 || !gpu_va_fits(in.gpu_va, in.map_bytes))
		return gpu_refuse("VA_MAP", "fit", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (in.handle == 0) {
		if ((in.flags & R5_GPU_VA_PRT) == 0 || in.bo_offset != 0)
			return gpu_refuse("VA_MAP", "bo", B_BAD_VALUE, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		if (gpu_prt_overlaps(client->address, in.gpu_va, in.map_bytes)
			|| gpu_va_bo_overlap(client->address, in.gpu_va, in.map_bytes))
			return gpu_refuse("VA_MAP", "overlap", B_BAD_VALUE, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		if (gR5GpuOps.vmid_prepare() != B_OK)
			return gpu_refuse("VA_MAP", "vmid", B_NO_INIT, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		for (uint64 page = 0; page < in.map_bytes; page += 4096) {
			if (gR5GpuOps.vmid_lookup(client->address, in.gpu_va + page) == B_OK) {
				gpu_va_clear(client->address, in.gpu_va, page);
				return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE, in.handle, 0,
					0, in.gpu_va + page, in.bo_offset, in.map_bytes, in.flags);
			}
		}
		status = gpu_va_write_prt(client->address, in.gpu_va, in.map_bytes);
		if (status != B_OK)
			return gpu_refuse("VA_MAP", "prt", status, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		status = gpu_prt_add(client->address, in.gpu_va, in.map_bytes);
		if (status != B_OK) {
			gpu_va_clear(client->address, in.gpu_va, in.map_bytes);
			return gpu_refuse("VA_MAP", "prt", status, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		}
		return B_OK;
	}
	if (!sReplacing && gpu_prt_overlaps(client->address, in.gpu_va, in.map_bytes))
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
	if (gR5GpuOps.vmid_prepare() != B_OK)
		return gpu_refuse("VA_MAP", "vmid", B_NO_INIT, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	uint32 flags = in.flags;
	if (flags == 0)
		flags = R5_GPU_VA_READ | R5_GPU_VA_WRITE;
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		uint32 fb = 0;
		if (gpu_vram_base(client->info, &fb) != B_OK)
			return gpu_refuse("VA_MAP", "vram", B_NO_INIT, in.handle, bo->domain,
				bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		uint64 phys = ((uint64)fb << 24) + bo->vramOffset + in.bo_offset;
		uint64 pte = 0;
		if (gR5GpuOps.encode_pte == NULL
			|| gR5GpuOps.encode_pte(phys, flags, 0, 0, &pte) != B_OK)
			return gpu_refuse("VA_MAP", "pte", B_NOT_SUPPORTED, in.handle,
				bo->domain, bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
				in.flags);
		for (uint64 page = 0; page < in.map_bytes; page += 4096) {
			if (gR5GpuOps.vmid_lookup(client->address, in.gpu_va + page) == B_OK) {
				gpu_va_clear(client->address, in.gpu_va, page);
				return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE, in.handle,
					bo->domain, bo->bytes, in.gpu_va + page, in.bo_offset,
					in.map_bytes, in.flags);
			}
			if (gR5GpuOps.vm_map(client->address, in.gpu_va + page, pte) != B_OK) {
				gpu_va_clear(client->address, in.gpu_va, page);
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
				gpu_va_clear(client->address, in.gpu_va, filled);
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
					if (gR5GpuOps.vmid_lookup(client->address, in.gpu_va + filled) == B_OK) {
						gpu_va_clear(client->address, in.gpu_va, filled);
						return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE,
							in.handle, bo->domain, bo->bytes,
							in.gpu_va + filled, in.bo_offset, in.map_bytes,
							in.flags);
					}
					uint64 pte = 0;
					if (gR5GpuOps.encode_pte == NULL
						|| gR5GpuOps.encode_pte(address, flags, 1, 0, &pte) != B_OK) {
						gpu_va_clear(client->address, in.gpu_va, filled);
						return gpu_refuse("VA_MAP", "pte", B_NOT_SUPPORTED,
							in.handle, bo->domain, bo->bytes,
							in.gpu_va + filled, in.bo_offset, in.map_bytes,
							in.flags);
					}
					if (gR5GpuOps.vm_map(client->address, in.gpu_va + filled, pte) != B_OK) {
						gpu_va_clear(client->address, in.gpu_va, filled);
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
			gpu_va_clear(client->address, in.gpu_va, filled);
			return gpu_refuse("VA_MAP", "short", B_BAD_DATA, in.handle,
				bo->domain, bo->bytes, in.gpu_va, in.bo_offset, filled,
				in.flags);
		}
	}
	if (gR5GpuOps.tlb_flush(client->address) != B_OK) {
		gpu_va_clear(client->address, in.gpu_va, in.map_bytes);
		return gpu_refuse("VA_MAP", "flush", B_ERROR, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	}
	bo->ranges[slot].live = true;
	bo->ranges[slot].va = in.gpu_va;
	bo->ranges[slot].offset = in.bo_offset;
	bo->ranges[slot].bytes = in.map_bytes;
	if (gpu_debug_enabled()) {
		dprintf("radeon_hd: va map %016llx bytes %llu domain %lu\n",
			(unsigned long long)in.gpu_va,
			(unsigned long long)in.map_bytes,
			(unsigned long)bo->domain);
	}
	return B_OK;
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
	if (gpu_bo_partial(client->address, in.gpu_va, in.map_bytes))
		return gpu_refuse(op, "overlap", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (gR5GpuOps.vmid_prepare() != B_OK)
		return gpu_refuse(op, "vmid", B_NO_INIT, in.handle, 0, 0, in.gpu_va,
			in.bo_offset, in.map_bytes, in.flags);
	gpu_va_clear(client->address, in.gpu_va, in.map_bytes);
	gpu_ranges_drop_contained(client->address, in.gpu_va, in.map_bytes);
	if (clearOnly) {
		gpu_prt_drop_contained(client->address, in.gpu_va, in.map_bytes);
		return B_OK;
	}
	if (in.handle == 0) {
		status = gpu_va_write_prt(client->address, in.gpu_va, in.map_bytes);
		if (status != B_OK)
			return gpu_refuse(op, "prt", status, in.handle, 0, 0, in.gpu_va,
				in.bo_offset, in.map_bytes, in.flags);
		status = gpu_prt_add(client->address, in.gpu_va, in.map_bytes);
		if (status != B_OK) {
			gpu_va_clear(client->address, in.gpu_va, in.map_bytes);
			return gpu_refuse(op, "prt", status, in.handle, 0, 0, in.gpu_va,
				in.bo_offset, in.map_bytes, in.flags);
		}
		return B_OK;
	}
	sReplacing = true;
	status = gpu_va_map(client, buffer, length);
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
	for (uint32 i = 0; i < kMaxRange; i++) {
		gpu_range* range = &bo->ranges[i];
		if (!range->live || range->va != in.gpu_va
			|| range->offset != in.bo_offset || range->bytes != in.map_bytes)
			continue;
		for (uint64 page = 0; page < range->bytes; page += 4096) {
			if (gR5GpuOps.vm_map(bo->space, range->va + page, 0) != B_OK)
				return gpu_refuse("VA_UNMAP", "pte", B_BAD_ADDRESS, in.handle,
					bo->domain, bo->bytes, range->va + page, in.bo_offset,
					in.map_bytes, in.flags);
		}
		if (gR5GpuOps.tlb_flush(bo->space) != B_OK)
			return gpu_refuse("VA_UNMAP", "flush", B_ERROR, in.handle, bo->domain,
				bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		range->live = false;
		return B_OK;
	}
	return gpu_refuse("VA_UNMAP", "range", B_BAD_VALUE, in.handle,
		bo != NULL ? bo->domain : 0, bo != NULL ? bo->bytes : 0, in.gpu_va,
		in.bo_offset, in.map_bytes, in.flags);
}


static bool
gpu_ib_mapped(void* space, uint64 va, uint32 dwords)
{
	uint64 first = 0;
	uint64 last = 0;
	if (!r5_ib_span(va, dwords, &first, &last))
		return false;
	for (uint64 page = first; ; page += 4096) {
		if (gR5GpuOps.vmid_lookup(space, page) != B_OK)
			return false;
		if (page == last)
			break;
	}
	return true;
}


static bool
gpu_sync_signalled(uint64 value, uint64 submitted, uint64 target, bool binary)
{
	if (binary)
		return submitted > 0 && value >= submitted;
	return value >= target;
}


static bool gpu_job_blocks(const gpu_sync* sync, uint64 target, bool binary);


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
	if (gpu_space_lost(client))
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
		if (!gpu_ib_mapped(client->address, ibs[i].gpu_va, ibs[i].dwords))
			return gpu_refuse("CS_SUBMIT", "mapped", B_BAD_ADDRESS, in.context, 0,
				ibs[i].dwords, ibs[i].gpu_va, 0, in.ring, ibs[i].flags);
	}
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
			if (gpu_space_lost(client))
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
	uint64 vas[kMaxList];
	uint32 dws[kMaxList];
	for (uint32 i = 0; i < in.ib_count; i++) {
		vas[i] = ibs[i].gpu_va;
		dws[i] = ibs[i].dwords;
	}
	/* Worst-case room, then the kernel-copy flush, then one submit that
	 * reserves the VMID and rings the bell while this lock is held. */
	if (gR5GpuOps.wait_room == NULL || gR5GpuOps.submit == NULL)
		return gpu_refuse("CS_SUBMIT", "room", B_NOT_SUPPORTED, in.context, 0,
			in.ib_count, ibs[0].gpu_va, 0, in.ring, 0);
	status = gR5GpuOps.wait_room(in.ib_count);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "room", status, in.context, 0, in.ib_count,
			ibs[0].gpu_va, 0, in.ring, 0);
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
			dprintf("radeon_hd: submit ib va %016llx dw %lu\n",
				(unsigned long long)ibs[i].gpu_va,
				(unsigned long)ibs[i].dwords);
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
	uint64 sequence = 0;
	status = gR5GpuOps.submit(client->address, vas, dws, in.ib_count, &sequence);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "ring", status, in.context, 0,
			in.ib_count, ibs[0].gpu_va, 0, in.ring, 0);
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
	job->owner = client;
	job->live = true;
	client->lastSequence = sequence;
	if (gpu_seen() >= sequence)
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
		if (gR5GpuOps.dump != NULL)
		gR5GpuOps.dump("fence timeout");
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
		if (gR5GpuOps.dump != NULL)
		gR5GpuOps.dump("sync wait");
	return status;
}


static bool
gpu_job_blocks(const gpu_sync* sync, uint64 target, bool binary)
{
	uint64 seen = gpu_seen();
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
		if (gpu_space_lost(client))
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


static void
gpu_sync_host(gpu_sync* sync);


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
	if (!gpu_src_reached(src, in.src_point) && gpu_space_lost(client))
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
	bool enabled = false;
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		enabled = get_driver_boolean_parameter(settings, "gpu_selftest",
			false, true);
		unload_driver_settings(settings);
	}
	return enabled;
}


static status_t
gpu_open(const char* name, uint32 flags, void** cookie)
{
	(void)name;
	(void)flags;
	if (!sReady || gDeviceInfo[0] == NULL)
		return B_ERROR;
	gpu_client* client = (gpu_client*)malloc(sizeof(gpu_client));
	if (client == NULL)
		return B_NO_MEMORY;
	memset(client, 0, sizeof(*client));
	client->info = gDeviceInfo[0];
	gpu_lock();
	if (gR5GpuOps.address_open == NULL
		|| gR5GpuOps.address_open(&client->address) != B_OK
		|| client->address == NULL) {
		gpu_unlock();
		free(client);
		return B_NO_MEMORY;
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
gpu_free(void* cookie)
{
	gpu_client* client = (gpu_client*)cookie;
	if (client == NULL)
		return B_OK;
	gpu_lock();
	status_t status = B_OK;
	uint64 seen = gpu_seen();
	r5_gpu_eop_at(seen);
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
			if (bo->domain == R5_GPU_DOMAIN_VRAM) {
				if (client->vramUsed >= bo->bytes)
					client->vramUsed -= bo->bytes;
			} else if (client->gttUsed >= bo->bytes)
				client->gttUsed -= bo->bytes;
			bo->owner = NULL;
			bo->parked = true;
			bo->parkSequence = sequence;
			if (gR5GpuOps.space_hold != NULL)
				gR5GpuOps.space_hold(bo->space);
		} else
			gpu_bo_release(bo);
	}
	for (uint32 i = 0; i < kMaxSync; i++) {
		client->syncs[i].live = false;
		client->syncs[i].owner = NULL;
	}
	if (client->contexts > 0) {
		sContexts -= client->contexts;
		if (sContexts < 0)
			sContexts = 0;
	}
	gpu_prt_release_space(client->address);
	if (gR5GpuOps.address_close != NULL)
		gR5GpuOps.address_close(client->address, seen);
	client->address = NULL;
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


static status_t
gpu_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	gpu_client* client = (gpu_client*)cookie;
	if (client == NULL || client->info == NULL)
		return B_BAD_VALUE;
	if (op == R5_GPU_GET_INFO)
		return gpu_get_info(client, client->info, buffer, length);
	if (op == R5_GPU_GET_COUNTERS) {
		r5_gpu_counters out;
		memset(&out, 0, sizeof(out));
		out.size = (uint32)sizeof(out);
		out.version = 1;
		gpu_lock();
		for (uint32 i = 0; i < kMaxBo; i++) {
			if (!sBo[i].live)
				continue;
			out.bo_count++;
			if (sBo[i].space == client->address)
				out.bo_count_space++;
		}
		if (gR5GpuOps.space_counts != NULL) {
			gR5GpuOps.space_counts(client->address, &out.pt_pages_space,
				&out.vmids_space, &out.pt_pages, &out.vmids);
		}
		gpu_unlock();
		return gpu_copy_out(buffer, length, &out, sizeof(out));
	}
	if (!gpu_render_enabled())
		return B_NOT_ALLOWED;
	if (op == R5_GPU_BO_EXPORT || op == R5_GPU_BO_IMPORT)
		return B_UNSUPPORTED;

	gpu_lock();
	sInfo = client->info;
	status_t status = B_BAD_VALUE;
	if (op == R5_GPU_READ_REG) {
		r5_gpu_read_reg in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if ((in.offset & R5_GPU_QUERY_VRAM_BASE) != 0)
			status = B_NOT_ALLOWED;
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
		else if (gR5GpuOps.vmid_prepare() != B_OK)
			status = B_NO_INIT;
		else {
			if (sContexts > 0)
				if (gpu_debug_enabled())
				dprintf("radeon_hd: vmid1 reused, clients %ld\n",
					(long)sContexts);
			sContexts++;
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
			status = gR5GpuOps.stage_p == NULL ? B_NOT_SUPPORTED
				: gR5GpuOps.stage_p(client->info, &in);
			status_t copied = gpu_copy_out(buffer, length, &in, sizeof(in));
			if (copied != B_OK)
				status = copied;
		}
	} else if (op == R5_GPU_SELFTEST) {
		if (sLost) {
			dprintf("radeon_hd: skipped, gpu lost\n");
			status = ECANCELED;
		} else if (!gpu_selftest_enabled())
			status = B_NOT_ALLOWED;
		else
			status = gR5GpuOps.stage_u == NULL ? B_NOT_SUPPORTED
				: gR5GpuOps.stage_u(client->info);
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
	} else
		status = B_UNSUPPORTED;
	gpu_unlock();
	return status;
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
