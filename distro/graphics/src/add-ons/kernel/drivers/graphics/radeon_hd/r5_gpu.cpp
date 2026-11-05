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
#include <driver_settings.h>
#include <stdlib.h>
#include <string.h>


extern radeon_info* gDeviceInfo[];


static const uint32 kGcSegment = 0x2000;
static const uint32 kShaderArrayConfig = 0x026f;
static const uint32 kUserShaderArrayConfig = 0x0270;
static const uint32 kGbAddrConfig = 0x063e;
static const uint32 kInactiveShift = 16;
static const uint32 kInactiveMask = 0xffff0000u;
static const uint32 kCuPerSh = 8;
static const uint32 kMaxBo = 64;
static const uint32 kMaxSync = 64;
static const uint32 kMaxClientBo = 32;
static const uint64 kMaxBoBytes = 64ull * 1024ull * 1024ull;
static const uint64 kRoomWaitUs = 100000;


static uint32
gpu_mmio(radeon_info* info, uint32 index)
{
	uint32 byte = (kGcSegment + index) * 4u;
	if (info == NULL || info->registers_area < 0 || info->registers == 0)
		return 0xffffffffu;
	return *(volatile uint32*)(info->registers + byte);
}


static uint32
gpu_active_cus(radeon_info* info, uint32* _bitmap, uint32* _gb)
{
	uint32 cc = gpu_mmio(info, kShaderArrayConfig);
	uint32 user = gpu_mmio(info, kUserShaderArrayConfig);
	uint32 inactive = ((cc | user) & kInactiveMask) >> kInactiveShift;
	uint32 mask = (1u << kCuPerSh) - 1u;
	uint32 bitmap = (~inactive) & mask;
	uint32 count = 0;
	for (uint32 bit = bitmap; bit != 0; bit >>= 1)
		count += bit & 1u;
	if (_bitmap != NULL)
		*_bitmap = bitmap;
	if (_gb != NULL)
		*_gb = gpu_mmio(info, kGbAddrConfig);
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


struct gpu_client {
	radeon_info*	info;
	uint32			handles[kMaxClientBo];
	uint32			handleCount;
	int32			contexts;
};


struct gpu_bo {
	bool			live;
	uint32			domain;
	uint32			flags;
	uint64			bytes;
	uint64			vramOffset;
	area_id			area;
	area_id			kernelArea;
	void*			user;
	gpu_client*		owner;
};


struct gpu_sync {
	bool			live;
	uint64			point;
	sem_id			sem;
};


static gpu_bo sBo[kMaxBo];
static gpu_sync sSync[kMaxSync];
static lock sGpuLock;
static bool sLockReady = false;
static bool sLost = false;
static int32 sContexts = 0;


static void
gpu_lock()
{
	if (!sLockReady) {
		init_lock(&sGpuLock, "r5 gpu");
		sLockReady = true;
	}
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
		why, (unsigned long)gpu_mmio(info, 0x01a0),
		(unsigned long)gpu_mmio(info, 0x0004),
		(unsigned long)r5_gfx9_user_seen());
}


static bool
gpu_copy_in(const void* buffer, size_t length, void* local, size_t bytes)
{
	if (buffer == NULL || length < bytes)
		return false;
	memcpy(local, buffer, bytes);
	return true;
}


static status_t
gpu_get_info(radeon_info* info, void* buffer, size_t length)
{
	if (buffer == NULL || length < 8 || info == NULL || info->pci == NULL)
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
	uint32 copy = (uint32)sizeof(out);
	if (length < copy)
		copy = (uint32)length;
	memcpy(buffer, &out, copy);
	if (length >= sizeof(out) + sizeof(r5_gpu_amd_info)) {
		r5_gpu_amd_info amd;
		memset(&amd, 0, sizeof(amd));
		amd.size = (uint32)sizeof(amd);
		amd.version = 1;
		amd.max_shader_engines = 1;
		amd.max_shader_arrays = 1;
		uint32 bitmap = 0;
		uint32 gb = 0;
		amd.active_cu = gpu_active_cus(info, &bitmap, &gb);
		amd.gb_addr_config = gb;
		uint32 revision = info->pci->revision;
		if (info->pci->device_id == 0x15e7 || info->pci->device_id == 0x1638)
			amd.external_revision = revision + 0xa1;
		else if (info->pci->device_id == 0x1636)
			amd.external_revision = revision + 0x91;
		memcpy((uint8*)buffer + sizeof(out), &amd, sizeof(amd));
	}
	return B_OK;
}


static void
gpu_bo_release(gpu_bo* bo)
{
	if (!bo->live)
		return;
	if (bo->area >= 0)
		delete_area(bo->area);
	if (bo->kernelArea >= 0)
		delete_area(bo->kernelArea);
	bo->live = false;
	bo->owner = NULL;
	bo->area = -1;
	bo->kernelArea = -1;
	bo->user = NULL;
}


static status_t
gpu_bo_create(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	if (in.bytes == 0 || in.bytes > kMaxBoBytes)
		return B_BAD_VALUE;
	if (in.domain != R5_GPU_DOMAIN_VRAM && in.domain != R5_GPU_DOMAIN_GTT)
		return B_BAD_VALUE;
	if (client->handleCount >= kMaxClientBo)
		return B_NO_MEMORY;
	uint64 bytes = (in.bytes + 4095ull) & ~4095ull;
	int slot = -1;
	for (uint32 i = 0; i < kMaxBo; i++) {
		if (!sBo[i].live) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0)
		return B_NO_MEMORY;
	gpu_bo* bo = &sBo[slot];
	memset(bo, 0, sizeof(*bo));
	bo->area = -1;
	bo->kernelArea = -1;
	bo->domain = in.domain;
	bo->flags = in.flags;
	bo->bytes = bytes;
	if (in.domain == R5_GPU_DOMAIN_VRAM) {
		uint64 offset = 0;
		uint8* address = NULL;
		if (client->info->shared_info == NULL)
			return B_NO_INIT;
		// The pool allocator is the kernel host. A VRAM page is mapped
		// into this team only when BO_MAP asks for it.
		if (r5_gfx9_alloc_vram(bytes, 4096, &offset, &address) != B_OK)
			return B_NO_MEMORY;
		bo->vramOffset = offset;
	} else {
		void* user = NULL;
		area_id area = create_area("r5 gpu bo", &user, B_ANY_ADDRESS,
			(size_t)bytes, B_FULL_LOCK,
			B_READ_AREA | B_WRITE_AREA);
		if (area < 0 || user == NULL)
			return B_NO_MEMORY;
		bo->area = area;
		bo->user = user;
	}
	bo->live = true;
	bo->owner = client;
	client->handles[client->handleCount++] = (uint32)slot + 1u;
	in.handle = (uint32)slot + 1u;
	in.area = bo->area;
	in.bytes = bytes;
	memcpy(buffer, &in, sizeof(in));
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
gpu_bo_free(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return B_BAD_VALUE;
	gpu_bo_release(bo);
	for (uint32 i = 0; i < client->handleCount; i++) {
		if (client->handles[i] == in.handle) {
			client->handles[i] = client->handles[client->handleCount - 1];
			client->handleCount--;
			break;
		}
	}
	return B_OK;
}


static status_t
gpu_bo_map(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return B_BAD_VALUE;
	if (bo->area >= 0) {
		in.area = bo->area;
		memcpy(buffer, &in, sizeof(in));
		return B_OK;
	}
	uint32 fb = gpu_mmio(client->info, 0x096b) & 0x00ffffffu;
	phys_addr_t phys = (phys_addr_t)(((uint64)fb << 24) + bo->vramOffset);
	void* kernel = NULL;
	area_id kernelArea = map_physical_memory("r5 gpu vram", phys,
		(size_t)bo->bytes, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &kernel);
	if (kernelArea < 0)
		return kernelArea;
	void* user = NULL;
	area_id area = clone_area("r5 gpu vram", &user, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, kernelArea);
	if (area < 0) {
		delete_area(kernelArea);
		return area;
	}
	bo->kernelArea = kernelArea;
	bo->area = area;
	bo->user = user;
	in.area = area;
	memcpy(buffer, &in, sizeof(in));
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


static status_t
gpu_va_map(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_va in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	if (in.context != 1 || (in.gpu_va & 0xfffull) != 0)
		return B_BAD_VALUE;
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return B_BAD_VALUE;
	uint64 end = in.gpu_va + bo->bytes;
	bool low = in.gpu_va >= R5_GPU_VA_OFFSET && end <= R5_GPU_VA_MAX;
	bool high = in.gpu_va >= R5_GPU_HIGH_VA_OFFSET && end <= R5_GPU_HIGH_VA_MAX;
	if (!low && !high)
		return B_BAD_VALUE;
	if (!r5_gfx9_vmid1_setup())
		return B_NO_INIT;
	uint32 flags = in.flags;
	if (flags == 0)
		flags = R5_GPU_VA_READ | R5_GPU_VA_WRITE;
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		uint32 fb = gpu_mmio(client->info, 0x096b) & 0x00ffffffu;
		uint64 phys = ((uint64)fb << 24) + bo->vramOffset;
		uint64 pte = r5_gfx9_gart_pte(phys, gpu_pte_flags(flags));
		for (uint64 page = 0; page < bo->bytes; page += 4096) {
			if (!r5_gfx9_vmid1_map(in.gpu_va + page, pte))
				return B_NO_MEMORY;
			pte += 4096;
		}
	} else {
		physical_entry entries[256];
		uint32 count = 256;
		status_t mapped = get_memory_map_etc(B_CURRENT_TEAM, bo->user,
			(size_t)bo->bytes, entries, &count);
		if (mapped != B_OK)
			return mapped;
		uint64 filled = 0;
		for (uint32 slot = 0; slot < count && filled < bo->bytes; slot++) {
			uint64 address = entries[slot].address;
			uint64 bytes = entries[slot].size;
			while (bytes >= 4096 && filled < bo->bytes) {
				uint64 pte = r5_gfx9_gart_pte(address,
					gpu_pte_flags(flags) | (uint64)R5_GFX9_PTE_SYSTEM
					| (uint64)R5_GFX9_PTE_SNOOPED);
				if (!r5_gfx9_vmid1_map(in.gpu_va + filled, pte))
					return B_NO_MEMORY;
				address += 4096;
				bytes -= 4096;
				filled += 4096;
			}
		}
		if (filled != bo->bytes)
			return B_BAD_DATA;
	}
	if (!r5_gfx9_vmid1_flush())
		return B_ERROR;
	return B_OK;
}


static status_t
gpu_submit(radeon_info* info, void* buffer, size_t length)
{
	if (sLost)
		return B_ERROR;
	r5_gpu_cs in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	if (in.context != 1 || in.ring != 0 || in.ib_dwords == 0)
		return B_BAD_VALUE;
	if ((in.ib_va & 3ull) != 0)
		return B_BAD_VALUE;
	if (!r5_gfx9_vmid1_lookup(in.ib_va))
		return B_BAD_ADDRESS;
	if (!r5_gfx9_user_room()) {
		gpu_unlock();
		sem_id sem = r5_gfx9_eop_sem();
		if (sem >= 0)
			acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, kRoomWaitUs);
		else
			snooze(kRoomWaitUs);
		gpu_lock();
		if (!r5_gfx9_user_room())
			return B_BUSY;
	}
	uint64 sequence = 0;
	if (!r5_gfx9_user_submit(in.ib_va, in.ib_dwords, &sequence))
		return B_ERROR;
	in.sequence = sequence;
	memcpy(buffer, &in, sizeof(in));
	(void)info;
	return B_OK;
}


static status_t
gpu_fence_wait(radeon_info* info, void* buffer, size_t length)
{
	r5_gpu_fence in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	if (r5_gfx9_user_seen() >= in.sequence)
		return B_OK;
	if (in.timeout_us == 0)
		return B_TIMED_OUT;
	sem_id sem = r5_gfx9_eop_sem();
	if (sem < 0)
		return B_NO_INIT;
	gpu_unlock();
	status_t status = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT,
		(bigtime_t)in.timeout_us);
	gpu_lock();
	if (r5_gfx9_user_seen() >= in.sequence)
		return B_OK;
	if (status == B_TIMED_OUT) {
		gpu_lost(info, "fence");
		return B_TIMED_OUT;
	}
	return status;
}


static status_t
gpu_sync_create(void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	int slot = -1;
	for (uint32 i = 0; i < kMaxSync; i++) {
		if (!sSync[i].live) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0)
		return B_NO_MEMORY;
	sem_id sem = create_sem(0, "r5 gpu sync");
	if (sem < 0)
		return sem;
	sSync[slot].live = true;
	sSync[slot].point = 0;
	sSync[slot].sem = sem;
	in.handle = (uint32)slot + 1u;
	memcpy(buffer, &in, sizeof(in));
	return B_OK;
}


static gpu_sync*
gpu_sync_get(uint32 handle)
{
	if (handle == 0 || handle > kMaxSync)
		return NULL;
	gpu_sync* sync = &sSync[handle - 1u];
	if (!sync->live)
		return NULL;
	return sync;
}


static status_t
gpu_sync_destroy(void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	gpu_sync* sync = gpu_sync_get(in.handle);
	if (sync == NULL)
		return B_BAD_VALUE;
	delete_sem(sync->sem);
	sync->live = false;
	sync->sem = -1;
	return B_OK;
}


static status_t
gpu_sync_wait(void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	gpu_sync* sync = gpu_sync_get(in.handle);
	if (sync == NULL)
		return B_BAD_VALUE;
	if (sync->point >= in.point)
		return B_OK;
	sem_id sem = sync->sem;
	bigtime_t left = in.timeout_us;
	gpu_unlock();
	status_t status = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, left);
	gpu_lock();
	sync = gpu_sync_get(in.handle);
	if (sync != NULL && sync->point >= in.point)
		return B_OK;
	if (status == B_TIMED_OUT)
		return B_TIMED_OUT;
	return status;
}


static status_t
gpu_sync_signal(void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
		return B_BAD_VALUE;
	gpu_sync* sync = gpu_sync_get(in.handle);
	if (sync == NULL)
		return B_BAD_VALUE;
	if (in.point > sync->point)
		sync->point = in.point;
	release_sem_etc(sync->sem, 1, B_DO_NOT_RESCHEDULE);
	return B_OK;
}


static status_t
gpu_open(const char* name, uint32 flags, void** cookie)
{
	(void)name;
	(void)flags;
	if (gDeviceInfo[0] == NULL)
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
	for (uint32 i = 0; i < client->handleCount; i++) {
		uint32 handle = client->handles[i];
		if (handle > 0 && handle <= kMaxBo)
			gpu_bo_release(&sBo[handle - 1u]);
	}
	if (client->contexts > 0) {
		sContexts -= client->contexts;
		if (sContexts < 0)
			sContexts = 0;
	}
	gpu_unlock();
	free(client);
	return B_OK;
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
		if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
			status = B_BAD_VALUE;
		else if (in.offset != kGbAddrConfig)
			status = B_NOT_ALLOWED;
		else {
			in.value = gpu_mmio(client->info, in.offset);
			memcpy(buffer, &in, sizeof(in));
			status = B_OK;
		}
	} else if (op == R5_GPU_CTX_CREATE) {
		r5_gpu_ctx in;
		if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
			status = B_BAD_VALUE;
		else if (!r5_gfx9_vmid1_setup())
			status = B_NO_INIT;
		else {
			sContexts++;
			client->contexts++;
			in.context = 1;
			memcpy(buffer, &in, sizeof(in));
			status = B_OK;
		}
	} else if (op == R5_GPU_CTX_DESTROY) {
		r5_gpu_ctx in;
		if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
			status = B_BAD_VALUE;
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
	else if (op == R5_GPU_VA_UNMAP) {
		r5_gpu_va in;
		if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
			status = B_BAD_VALUE;
		else {
			gpu_bo* bo = gpu_bo_get(client, in.handle);
			if (bo == NULL || in.context != 1)
				status = B_BAD_VALUE;
			else {
				status = B_OK;
				for (uint64 page = 0; page < bo->bytes; page += 4096) {
					if (!r5_gfx9_vmid1_map(in.gpu_va + page, 0))
						status = B_BAD_ADDRESS;
				}
				if (status == B_OK && !r5_gfx9_vmid1_flush())
					status = B_ERROR;
			}
		}
	}
	else if (op == R5_GPU_CS_SUBMIT)
		status = gpu_submit(client->info, buffer, length);
	else if (op == R5_GPU_FENCE_WAIT)
		status = gpu_fence_wait(client->info, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_CREATE)
		status = gpu_sync_create(buffer, length);
	else if (op == R5_GPU_SYNCOBJ_DESTROY)
		status = gpu_sync_destroy(buffer, length);
	else if (op == R5_GPU_SYNCOBJ_WAIT)
		status = gpu_sync_wait(buffer, length);
	else if (op == R5_GPU_SYNCOBJ_SIGNAL)
		status = gpu_sync_signal(buffer, length);
	else if (op == R5_GPU_SYNCOBJ_RESET || op == R5_GPU_SYNCOBJ_QUERY) {
		r5_gpu_syncobj in;
		if (!gpu_copy_in(buffer, length, &in, sizeof(in)))
			status = B_BAD_VALUE;
		else {
			gpu_sync* sync = gpu_sync_get(in.handle);
			if (sync == NULL)
				status = B_BAD_VALUE;
			else if (op == R5_GPU_SYNCOBJ_RESET) {
				sync->point = 0;
				status = B_OK;
			} else {
				in.point = sync->point;
				memcpy(buffer, &in, sizeof(in));
				status = B_OK;
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
