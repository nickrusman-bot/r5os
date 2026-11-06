/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Render node /dev/gpu/radeon_hd/<n>. The graphics node is unchanged.
 * Without gpu_render the node only answers GET_INFO. VMID 1 is one
 * shared address space. A fence timeout marks the device lost and
 * does not reset it.
 */

#include "radeon_hd_private.h"
#include "gfx9_start.h"
#include "r5_gpu.h"
#include "r5_span.h"
#include "r5_wait.h"

#include <KernelExport.h>
#include <condition_variable.h>
#include <kernel.h>

#include <driver_settings.h>
#include <stdlib.h>
#include <string.h>


extern radeon_info* gDeviceInfo[];


static const uint32 kGcSegment = 0x2000;
static const uint32 kNbioSegment = 0x0D20;
static const uint32 kStrapIndex = 0x000f;
static const uint32 kStrapMask = 0x0F000000u;
static const uint32 kStrapShift = 24;
static const uint32 kShaderArrayConfig = 0x026f;
static const uint32 kUserShaderArrayConfig = 0x0270;
static const uint32 kGbAddrConfig = 0x063e;
static const uint32 kInactiveShift = 16;
static const uint32 kInactiveMask = 0xffff0000u;
static const uint32 kCuPerSh = 8;
static const uint32 kMaxBo = 256;
static const uint32 kMaxClientBo = 256;
static const uint32 kMaxSync = 128;
static const uint32 kMaxRange = 8;
static const uint32 kMaxJob = 64;
static const uint32 kMaxList = 8;
static const uint64 kMaxBoBytes = 64ull * 1024ull * 1024ull;
static const uint64 kRoomWaitUs = 100000;
static const uint64 kFreeWaitUs = 5000000;

static uint32 sRefuseCount;


/* The first 32 refusals in a boot. Later ones still return the status. */
static status_t
gpu_refuse(const char* op, const char* why, status_t status, uint32 handle,
	uint32 domain, uint64 bytes, uint64 va, uint64 offset, uint64 mapBytes,
	uint32 flags)
{
	if (sRefuseCount < 32) {
		sRefuseCount++;
		dprintf("radeon_hd: refuse %s %s status %#x handle %lu domain %lu "
			"bytes %llu va %#llx offset %llu map %llu flags %#lx\n",
			op, why, (unsigned)status, (unsigned long)handle,
			(unsigned long)domain, (unsigned long long)bytes,
			(unsigned long long)va, (unsigned long long)offset,
			(unsigned long long)mapBytes, (unsigned long)flags);
	}
	return status;
}


static uint32
gpu_mmio(radeon_info* info, uint32 segment, uint32 index)
{
	uint32 byte = (segment + index) * 4u;
	if (info == NULL || info->registers_area < 0 || info->registers == 0)
		return 0xffffffffu;
	return *(volatile uint32*)(info->registers + byte);
}


static uint32
gpu_strap_rev(radeon_info* info)
{
	/* nbio_v7_0_get_rev_id: mmRCC_DEV0_EPF0_STRAP0 in NBIO segment 2. */
	uint32 strap = gpu_mmio(info, kNbioSegment, kStrapIndex);
	return (strap & kStrapMask) >> kStrapShift;
}


static uint32
gpu_active_cus(radeon_info* info, uint32* _bitmap)
{
	uint32 cc = gpu_mmio(info, kGcSegment, kShaderArrayConfig);
	uint32 user = gpu_mmio(info, kGcSegment, kUserShaderArrayConfig);
	uint32 inactive = ((cc | user) & kInactiveMask) >> kInactiveShift;
	uint32 mask = (1u << kCuPerSh) - 1u;
	uint32 bitmap = (~inactive) & mask;
	uint32 count = 0;
	for (uint32 bit = bitmap; bit != 0; bit >>= 1)
		count += bit & 1u;
	if (_bitmap != NULL)
		*_bitmap = bitmap;
	dprintf("radeon_hd: gfx9 cu cc %08lx user %08lx active %lu bitmap %08lx\n",
		(unsigned long)cc, (unsigned long)user, (unsigned long)count,
		(unsigned long)bitmap);
	return count;
}


static bool
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
	area_id			kernelArea;
	void*			kernel;
	gpu_client*		owner;
	gpu_range		ranges[kMaxRange];
};


struct gpu_job {
	bool			live;
	uint64			sequence;
	uint32			count;
	gpu_sync*		syncs[kMaxList];
	uint64			points[kMaxList];
};


struct gpu_client {
	radeon_info*	info;
	uint32			handles[kMaxClientBo];
	uint32			handleCount;
	int32			contexts;
	uint64			lastSequence;
	gpu_sync		syncs[kMaxSync];
};


static gpu_bo sBo[kMaxBo];
static gpu_job sJobs[kMaxJob];
static uint64 sHighestSequence;
static lock sGpuLock;
static ConditionVariable sEop;
static bool sReady = false;
static bool sLost = false;
static int32 sContexts = 0;
static sem_id sRetireSem = -1;
static thread_id sRetireThread = -1;


static void
gpu_lock()
{
	acquire_lock(&sGpuLock);
}


static void
gpu_unlock()
{
	release_lock(&sGpuLock);
}


static void
gpu_lost(radeon_info* info, const char* why)
{
	if (sLost)
		return;
	sLost = true;
	dprintf("radeon_hd: gpu lost %s cp %08lx grbm %08lx seen %lu\n",
		why, (unsigned long)gpu_mmio(info, kGcSegment, 0x01a0),
		(unsigned long)gpu_mmio(info, kGcSegment, 0x0004),
		(unsigned long)r5_gfx9_user_seen());
}


static status_t
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


static status_t
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


static void r5_gpu_eop();
static void gpu_sync_host(gpu_sync* sync);


static status_t
gpu_retire_thread(void* data)
{
	(void)data;
	while (acquire_sem(sRetireSem) == B_OK) {
		if (!sReady)
			break;
		gpu_lock();
		r5_gpu_eop();
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
r5_gpu_eop()
{
	if (!sReady)
		return;
	uint64 seen = r5_gfx9_user_seen();
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
		return r5_gfx9_user_seen();
	return *value;
}


static status_t
gpu_wait_value(radeon_info* info, volatile uint64* value, uint64 target,
	uint64 timeoutUs, bool lose)
{
	int step = r5_wait_step(gpu_counter(value), target, timeoutUs, 0);
	if (step == R5_WAIT_DONE)
		return B_OK;
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
		if (step == R5_WAIT_TIMEOUT) {
			if (lose)
				gpu_lost(info, "fence");
			return B_TIMED_OUT;
		}
		bigtime_t left = deadline - now;
		gpu_unlock();
		entry.Wait(B_RELATIVE_TIMEOUT, left);
		gpu_lock();
	}
}


static status_t
gpu_get_info(radeon_info* info, void* buffer, size_t length)
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
	out.vendor = 0x1002;
	out.device = info->pci->device_id;
	out.revision = info->pci->revision;
	if (gpu_render_enabled())
		out.capabilities = R5_GPU_CAP_RENDER;
	out.va_offset = R5_GPU_VA_OFFSET;
	out.va_max = R5_GPU_VA_MAX;
	out.high_va_offset = R5_GPU_HIGH_VA_OFFSET;
	out.high_va_max = R5_GPU_HIGH_VA_MAX;
	out.vendor_block_size = (uint32)sizeof(r5_gpu_amd_info);
	out.vendor_block_version = 1;
	out.bus = info->pci->bus;
	out.dev = info->pci->device;
	out.func = info->pci->function;
	out.subsystem_vendor = info->pci->u.h0.subsystem_vendor_id;
	out.subsystem_device = info->pci->u.h0.subsystem_id;
	if (info->shared_info != NULL) {
		out.vram_bytes = (uint64)info->shared_info->frame_buffer_size * 1024ull;
		out.gtt_bytes = out.vram_bytes;
	}
	size_t copy = sizeof(out);
	if (length < copy)
		copy = length;
	if (user_memcpy(buffer, &out, copy) != B_OK)
		return B_BAD_ADDRESS;
	if (length < sizeof(out) + sizeof(r5_gpu_amd_info))
		return B_OK;

	r5_gpu_amd_info amd;
	memset(&amd, 0, sizeof(amd));
	amd.size = (uint32)sizeof(amd);
	amd.version = 1;
	uint32 strap = gpu_strap_rev(info);
	uint32 bitmap = 0;
	amd.device.device_id = info->pci->device_id;
	amd.device.chip_rev = strap;
	amd.device.external_rev = r5_amd_external_rev(info->pci->device_id, strap);
	amd.device.pci_rev = info->pci->revision;
	amd.device.family = R5_AMDGPU_FAMILY_RV;
	amd.device.num_shader_engines = 1;
	amd.device.num_shader_arrays_per_engine = 1;
	amd.device.cu_active_number = gpu_active_cus(info, &bitmap);
	amd.device.cu_bitmap[0][0] = bitmap;
	amd.device.num_cu_per_sh = kCuPerSh;
	amd.device.ids_flags = R5_AMDGPU_IDS_FLAGS_FUSION;
	/* kHz. Mesa's captured amdgpu query for PCI 15e7 (Renoir) reports
	 * gpu_counter_freq 100000. Linux copies adev->clock.xclk into that
	 * field. A zero makes RADV warn that timestamps will be wrong. */
	if (info->pci->device_id == 0x15e7)
		amd.device.gpu_counter_freq = 100000;
	amd.device.virtual_address_offset = R5_GPU_VA_OFFSET;
	amd.device.virtual_address_max = R5_GPU_VA_MAX;
	amd.device.virtual_address_alignment = 4096;
	amd.device.gart_page_size = 4096;
	amd.device.wave_front_size = 64;
	amd.device.high_va_offset = R5_GPU_HIGH_VA_OFFSET;
	amd.device.high_va_max = R5_GPU_HIGH_VA_MAX;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_major = 9;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_minor = 3;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].ib_start_alignment = 32;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].ib_size_alignment = 4;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].available_rings = 1;
	amd.hw_ip[R5_AMDGPU_HW_IP_COMPUTE] = amd.hw_ip[R5_AMDGPU_HW_IP_GFX];
	amd.hw_ip[R5_AMDGPU_HW_IP_COMPUTE].available_rings = 0;
	for (uint32 i = 0; i < R5_AMDGPU_FW_COUNT && i < 32; i++) {
		amd.firmware[i].ver = info->fw_version[i];
		amd.firmware[i].feature = info->fw_feature[i];
	}
	if (user_memcpy((uint8*)buffer + sizeof(out), &amd, sizeof(amd)) != B_OK)
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
		for (uint64 page = 0; page < range->bytes; page += 4096)
			r5_gfx9_vmid1_map(range->va + page, 0);
		range->live = false;
		any = true;
	}
	if (any)
		r5_gfx9_vmid1_flush();
}


static void
gpu_bo_release(gpu_bo* bo)
{
	if (!bo->live)
		return;
	gpu_bo_unmap(bo);
	if (bo->domain == R5_GPU_DOMAIN_VRAM)
		r5_gfx9_free_user_vram(bo->vramOffset);
	if (bo->area >= 0)
		delete_area(bo->area);
	if (bo->kernelArea >= 0)
		delete_area(bo->kernelArea);
	bo->live = false;
	bo->owner = NULL;
	bo->area = -1;
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
	bo->kernelArea = -1;
	bo->domain = in.domain;
	bo->flags = in.flags;
	bo->bytes = bytes;
	if (in.domain == R5_GPU_DOMAIN_VRAM) {
		uint64 offset = 0;
		if (r5_gfx9_alloc_user_vram(bytes, &offset) != B_OK)
			return gpu_refuse("BO_CREATE", "vram", B_NO_MEMORY, 0, in.domain,
				bytes, 0, 0, 0, in.flags);
		bo->vramOffset = offset;
	} else {
		void* kernel = NULL;
		area_id area = create_area("r5 gpu gtt", &kernel,
			B_ANY_KERNEL_ADDRESS, (size_t)bytes, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
		if (area < 0 || kernel == NULL)
			return gpu_refuse("BO_CREATE", "gtt", area < 0 ? area : B_NO_MEMORY,
				0, in.domain, bytes, 0, 0, 0, in.flags);
		bo->kernelArea = area;
		bo->kernel = kernel;
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


static status_t
gpu_wait_client(gpu_client* client)
{
	if (client->lastSequence == 0)
		return B_OK;
	return gpu_wait_value(client->info, NULL, client->lastSequence,
		kFreeWaitUs, true);
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
	status = gpu_wait_client(client);
	bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return status != B_OK ? status : B_BAD_VALUE;
	gpu_bo_release(bo);
	for (uint32 i = 0; i < client->handleCount; i++) {
		if (client->handles[i] == in.handle) {
			client->handles[i] = client->handles[client->handleCount - 1];
			client->handleCount--;
			break;
		}
	}
	return status;
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
	if (bo->area >= 0) {
		in.area = bo->area;
		return gpu_copy_out(buffer, length, &in, sizeof(in));
	}
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		uint32 fb = gpu_mmio(client->info, kGcSegment, 0x096b) & 0x00ffffffu;
		phys_addr_t phys = (phys_addr_t)(((uint64)fb << 24) + bo->vramOffset);
		void* kernel = NULL;
		area_id kernelArea = map_physical_memory("r5 gpu vram", phys,
			(size_t)bo->bytes, B_ANY_KERNEL_ADDRESS,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
			&kernel);
		if (kernelArea < 0)
			return gpu_refuse("BO_MAP", "vram", kernelArea, in.handle, bo->domain,
				bo->bytes, 0, bo->vramOffset, 0, bo->flags);
		bo->kernelArea = kernelArea;
		bo->kernel = kernel;
	}
	void* user = NULL;
	area_id area = clone_area("r5 gpu bo", &user, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, bo->kernelArea);
	if (area < 0)
		return gpu_refuse("BO_MAP", "clone", area, in.handle, bo->domain,
			bo->bytes, 0, bo->vramOffset, 0, bo->flags);
	bo->area = area;
	in.area = area;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_MAP", "copy", status, in.handle, bo->domain,
			bo->bytes, 0, bo->vramOffset, 0, bo->flags);
	return B_OK;
}


static uint64
gpu_pte_flags(uint32 flags)
{
	uint64 bits = (uint64)R5_GFX9_PTE_VALID | (uint64)R5_GFX9_PTE_READABLE;
	if ((flags & R5_GPU_VA_WRITE) != 0)
		bits |= (uint64)R5_GFX9_PTE_WRITEABLE;
	if ((flags & R5_GPU_VA_EXEC) != 0)
		bits |= (uint64)R5_GFX9_PTE_EXECUTABLE;
	return bits;
}


static bool
gpu_va_fits(uint64 va, uint64 bytes)
{
	/* Linux amdgpu_gem_va_ioctl rejects an address in the hole, then
	 * keeps va_address & AMDGPU_GMC_HOLE_MASK. That mask is
	 * 0x0000ffffffffffff (bits 0-47). The walk uses bits 12-47 only,
	 * so a canonical high address and that masked address select the
	 * same page-table slots.
	 */
	if (bytes == 0 || (va & 0xfffull) != 0 || (bytes & 0xfffull) != 0)
		return false;
	uint64 end = va + bytes;
	if (end < va)
		return false;
	bool low = va >= R5_GPU_VA_OFFSET && end <= R5_GPU_VA_MAX;
	bool high = va >= R5_GPU_HIGH_VA_OFFSET && end <= R5_GPU_HIGH_VA_MAX;
	return low || high;
}


static void
gpu_va_clear(uint64 va, uint64 bytes)
{
	for (uint64 page = 0; page < bytes; page += 4096)
		r5_gfx9_vmid1_map(va + page, 0);
	if (bytes != 0)
		r5_gfx9_vmid1_flush();
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
	if (!r5_gfx9_vmid1_setup())
		return gpu_refuse("VA_MAP", "vmid", B_NO_INIT, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	uint32 flags = in.flags;
	if (flags == 0)
		flags = R5_GPU_VA_READ | R5_GPU_VA_WRITE;
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		uint32 fb = gpu_mmio(client->info, kGcSegment, 0x096b) & 0x00ffffffu;
		uint64 phys = ((uint64)fb << 24) + bo->vramOffset + in.bo_offset;
		uint64 pte = r5_gfx9_gart_pte(phys, gpu_pte_flags(flags));
		for (uint64 page = 0; page < in.map_bytes; page += 4096) {
			if (r5_gfx9_vmid1_lookup(in.gpu_va + page)) {
				gpu_va_clear(in.gpu_va, page);
				return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE, in.handle,
					bo->domain, bo->bytes, in.gpu_va + page, in.bo_offset,
					in.map_bytes, in.flags);
			}
			if (!r5_gfx9_vmid1_map(in.gpu_va + page, pte)) {
				gpu_va_clear(in.gpu_va, page);
				return gpu_refuse("VA_MAP", "pte", B_NO_MEMORY, in.handle,
					bo->domain, bo->bytes, in.gpu_va + page, in.bo_offset,
					in.map_bytes, in.flags);
			}
			pte += 4096;
		}
	} else {
		physical_entry entries[256];
		int32 count = get_memory_map(
			(uint8*)bo->kernel + in.bo_offset, (size_t)in.map_bytes,
			entries, 256);
		if (count < 0)
			return gpu_refuse("VA_MAP", "phys", count, in.handle, bo->domain,
				bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		uint64 filled = 0;
		for (int32 entry = 0; entry < count && filled < in.map_bytes; entry++) {
			uint64 address = entries[entry].address;
			uint64 bytes = entries[entry].size;
			while (bytes >= 4096 && filled < in.map_bytes) {
				if (r5_gfx9_vmid1_lookup(in.gpu_va + filled)) {
					gpu_va_clear(in.gpu_va, filled);
					return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE, in.handle,
						bo->domain, bo->bytes, in.gpu_va + filled, in.bo_offset,
						in.map_bytes, in.flags);
				}
				uint64 pte = r5_gfx9_gart_pte(address,
					gpu_pte_flags(flags) | (uint64)R5_GFX9_PTE_SYSTEM
					| (uint64)R5_GFX9_PTE_SNOOPED);
				if (!r5_gfx9_vmid1_map(in.gpu_va + filled, pte)) {
					gpu_va_clear(in.gpu_va, filled);
					return gpu_refuse("VA_MAP", "pte", B_NO_MEMORY, in.handle,
						bo->domain, bo->bytes, in.gpu_va + filled, in.bo_offset,
						in.map_bytes, in.flags);
				}
				address += 4096;
				bytes -= 4096;
				filled += 4096;
			}
		}
		if (filled != in.map_bytes) {
			gpu_va_clear(in.gpu_va, filled);
			return gpu_refuse("VA_MAP", "short", B_BAD_DATA, in.handle, bo->domain,
				bo->bytes, in.gpu_va, in.bo_offset, filled, in.flags);
		}
	}
	if (!r5_gfx9_vmid1_flush()) {
		gpu_va_clear(in.gpu_va, in.map_bytes);
		return gpu_refuse("VA_MAP", "flush", B_ERROR, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	}
	bo->ranges[slot].live = true;
	bo->ranges[slot].va = in.gpu_va;
	bo->ranges[slot].offset = in.bo_offset;
	bo->ranges[slot].bytes = in.map_bytes;
	return B_OK;
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
			if (!r5_gfx9_vmid1_map(range->va + page, 0))
				return gpu_refuse("VA_UNMAP", "pte", B_BAD_ADDRESS, in.handle,
					bo->domain, bo->bytes, range->va + page, in.bo_offset,
					in.map_bytes, in.flags);
		}
		if (!r5_gfx9_vmid1_flush())
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
gpu_ib_mapped(uint64 va, uint32 dwords)
{
	if (dwords == 0 || (va & 3ull) != 0)
		return false;
	uint64 bytes = (uint64)dwords * 4ull;
	uint64 first = va & ~0xfffull;
	uint64 last = (va + bytes - 1ull) & ~0xfffull;
	for (uint64 page = first; page <= last; page += 4096) {
		if (!r5_gfx9_vmid1_lookup(page))
			return false;
	}
	return true;
}


static status_t
gpu_wait_room(radeon_info* info, uint32 dwords)
{
	(void)info;
	if (r5_gfx9_user_room(dwords))
		return B_OK;
	bigtime_t deadline = system_time() + (bigtime_t)kRoomWaitUs;
	while (!r5_gfx9_user_room(dwords)) {
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		bigtime_t now = system_time();
		if (r5_gfx9_user_room(dwords))
			return B_OK;
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
	return gpu_sync_signalled(sync->point, sync->submitted, wait->point, binary);
}


static status_t
gpu_submit(gpu_client* client, void* buffer, size_t length)
{
	if (sLost)
		return gpu_refuse("CS_SUBMIT", "lost", B_ERROR, 0, 0, 0, 0, 0, 0, 0);
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
		if (!gpu_ib_mapped(ibs[i].gpu_va, ibs[i].dwords))
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
			if (sLost)
				return gpu_refuse("CS_SUBMIT", "lost", B_ERROR, in.context, 0,
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
	r5_gfx9_user_ib ringIbs[kMaxList];
	uint32 dwords = 8;
	for (uint32 i = 0; i < in.ib_count; i++) {
		ringIbs[i].va = ibs[i].gpu_va;
		ringIbs[i].dwords = ibs[i].dwords;
		dwords += 4;
	}
	status = gpu_wait_room(client->info, dwords);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "room", status, in.context, 0, in.ib_count,
			ibs[0].gpu_va, 0, in.ring, 0);
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
	if (!r5_gfx9_user_submit(ringIbs, in.ib_count, &sequence))
		return gpu_refuse("CS_SUBMIT", "ring", B_ERROR, in.context, 0,
			in.ib_count, ibs[0].gpu_va, 0, in.ring, 0);
	job->sequence = sequence;
	if (sequence > sHighestSequence)
		sHighestSequence = sequence;
	for (uint32 i = 0; i < in.signal_count; i++) {
		if ((signalFlags[i] & R5_GPU_CS_SYNC_BINARY) != 0)
			job->points[i] = sequence;
		gpu_sync* sync = job->syncs[i];
		if (sync != NULL && job->points[i] > sync->submitted)
			sync->submitted = job->points[i];
	}
	job->live = true;
	client->lastSequence = sequence;
	if (r5_gfx9_user_seen() >= sequence)
		r5_gpu_eop();
	in.sequence = sequence;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "copy", status, in.context, 0, in.ib_count,
			ibs[0].gpu_va, 0, in.ring, 0);
	return B_OK;
}


static status_t
gpu_fence_wait(radeon_info* info, void* buffer, size_t length)
{
	r5_gpu_fence in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	return gpu_wait_value(info, NULL, in.sequence, in.timeout_us, true);
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
				return B_BUSY;
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
		return gpu_wait_value(client->info, &sync->point, sync->submitted,
			in.timeout_us, false);
	}
	return gpu_wait_value(client->info, &sync->point, in.point, in.timeout_us,
		false);
}


static bool
gpu_list_ready(const gpu_sync* sync, uint64 target, uint32 flags)
{
	bool binary = (flags & R5_GPU_WAIT_BINARY) != 0;
	bool available = (flags & R5_GPU_WAIT_AVAILABLE) != 0;
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
	status_t status = gpu_wait_client(client);
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
		if (handle > 0 && handle <= kMaxBo)
			gpu_bo_release(&sBo[handle - 1u]);
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
		return gpu_get_info(client->info, buffer, length);
	if (!gpu_render_enabled())
		return B_NOT_ALLOWED;
	if (op == R5_GPU_BO_EXPORT || op == R5_GPU_BO_IMPORT)
		return B_UNSUPPORTED;

	gpu_lock();
	status_t status = B_BAD_VALUE;
	if (op == R5_GPU_READ_REG) {
		r5_gpu_read_reg in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (in.offset != kGbAddrConfig)
			status = B_NOT_ALLOWED;
		else {
			in.value = gpu_mmio(client->info, kGcSegment, in.offset);
			status = gpu_copy_out(buffer, length, &in, sizeof(in));
		}
	} else if (op == R5_GPU_CTX_CREATE) {
		r5_gpu_ctx in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (!r5_gfx9_vmid1_setup())
			status = B_NO_INIT;
		else {
			if (sContexts > 0)
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
