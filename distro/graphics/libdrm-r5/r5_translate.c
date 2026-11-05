/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Translate the AMDGPU ioctls libdrm issues into the R5 GPU interface.
 * Every R5 call uses the four-argument ioctl. Unknown AMDGPU commands
 * fail once with ENOSYS.
 */

#include "r5_translate.h"
#include "r5_gpu.h"
#include "libdrm_macros.h"

#include <drm.h>
#include <amdgpu_drm.h>

#include <xf86drm.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#if defined(__HAIKU__)
#include <OS.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#define R5_IOC_NR(request) ((unsigned)(request) & 0xffu)

static unsigned char sUnknownSeen[32];


static int
r5_fail(int err)
{
	errno = err;
	return -1;
}


/* Linux returns -errno. Haiku errno values are already negative. */
static inline int
r5_neg(int err)
{
	return err < 0 ? err : -err;
}


static int
r5_call(int fd, unsigned long op, void *buf, size_t bytes)
{
#if defined(__HAIKU__)
	if (ioctl(fd, op, buf, bytes) < 0)
		return -1;
	return 0;
#elif defined(R5_HOST_TEST)
	int r5_host_ioctl(int, unsigned long, void *, size_t);
	int ret = r5_host_ioctl(fd, op, buf, bytes);
	if (ret < 0) {
#if defined(R5_HOST_NEGATIVE_ERRNO)
		/* The fake kernel returns the Haiku errno, which is already negative. */
		errno = ret;
#else
		errno = -ret;
#endif
		return -1;
	}
	return 0;
#else
	(void)fd;
	(void)op;
	(void)buf;
	(void)bytes;
	return r5_fail(ENOSYS);
#endif
}


#if defined(__HAIKU__)
static void *
r5_area_address(int area)
{
	area_info info;
	if (get_area_info(area, &info) != B_OK)
		return NULL;
	return info.address;
}

static int
r5_area_destroy(int area)
{
	return delete_area(area) == B_OK ? 0 : -1;
}
#elif defined(R5_HOST_TEST)
void *r5_area_address(int area);
int r5_area_destroy(int area);
#endif

static void
r5_unknown(unsigned nr, const char *name)
{
	unsigned byte = nr / 8u;
	unsigned bit = 1u << (nr % 8u);
	if (byte >= sizeof(sUnknownSeen) || (sUnknownSeen[byte] & bit) != 0)
		return;
	sUnknownSeen[byte] |= bit;
	if (name != NULL)
		fprintf(stderr, "r5 libdrm: %s is not translated\n", name);
	else
		fprintf(stderr, "r5 libdrm: ioctl nr %#x is not translated\n", nr);
}


static int
r5_info(int fd, r5_gpu_info *info, r5_gpu_amd_info *amd)
{
	unsigned char bytes[sizeof(*info) + sizeof(*amd)];
	memset(bytes, 0, sizeof(bytes));
	r5_gpu_info *head = (r5_gpu_info *)bytes;
	head->size = (uint32_t)sizeof(*head);
	head->version = 1;
	if (r5_call(fd, R5_GPU_GET_INFO, bytes, sizeof(bytes)) != 0)
		return -1;
	memcpy(info, bytes, sizeof(*info));
	memcpy(amd, bytes + sizeof(*info), sizeof(*amd));
	return 0;
}


static int
r5_copy_out(void *dest, uint32_t destBytes, const void *src, uint32_t srcBytes)
{
	if (dest == NULL || destBytes == 0)
		return r5_fail(EINVAL);
	uint32_t bytes = destBytes < srcBytes ? destBytes : srcBytes;
	memcpy(dest, src, bytes);
	if (bytes < destBytes)
		memset((unsigned char *)dest + bytes, 0, destBytes - bytes);
	return 0;
}


static void
r5_version_string(char *dest, size_t *len, const char *src)
{
	size_t full = strlen(src);
	size_t room = dest == NULL ? 0 : *len;
	size_t copy = room < full ? room : full;
	if (dest != NULL && copy > 0)
		memcpy(dest, src, copy);
	*len = full;
}


static int
r5_version(void *arg)
{
	struct drm_version *version = arg;
	const char *name = "amdgpu";
	const char *date = "20260101";
	const char *desc = "R5 AMDGPU";
	version->version_major = 3;
	version->version_minor = 42;
	version->version_patchlevel = 0;
	{
		size_t len = version->name_len;
		r5_version_string(version->name, &len, name);
		version->name_len = len;
		len = version->date_len;
		r5_version_string(version->date, &len, date);
		version->date_len = len;
		len = version->desc_len;
		r5_version_string(version->desc, &len, desc);
		version->desc_len = len;
	}
	return 0;
}


static int
r5_amdgpu_info(int fd, struct drm_amdgpu_info *query)
{
	r5_gpu_info info;
	r5_gpu_amd_info amd;
	memset(&info, 0, sizeof(info));
	memset(&amd, 0, sizeof(amd));
	if (r5_info(fd, &info, &amd) != 0)
		return -1;
	void *out = (void *)(uintptr_t)query->return_pointer;
	if (query->query == AMDGPU_INFO_ACCEL_WORKING) {
		uint32_t working = (info.capabilities & R5_GPU_CAP_RENDER) ? 1u : 0u;
		return r5_copy_out(out, query->return_size, &working, sizeof(working));
	}
	if (query->query == AMDGPU_INFO_DEV_INFO)
		return r5_copy_out(out, query->return_size, &amd.device, sizeof(amd.device));
	if (query->query == AMDGPU_INFO_FW_VERSION) {
		uint32_t type = query->query_fw.fw_type;
		if (type >= R5_AMDGPU_FW_COUNT)
			return r5_fail(EINVAL);
		return r5_copy_out(out, query->return_size, &amd.firmware[type],
			sizeof(amd.firmware[type]));
	}
	if (query->query == AMDGPU_INFO_HW_IP_INFO) {
		uint32_t type = query->query_hw_ip.type;
		if (type >= R5_AMDGPU_HW_IP_COUNT)
			return r5_fail(EINVAL);
		return r5_copy_out(out, query->return_size, &amd.hw_ip[type],
			sizeof(amd.hw_ip[type]));
	}
	if (query->query == AMDGPU_INFO_HW_IP_COUNT) {
		uint32_t count = 0;
		if (query->query_hw_ip.type == AMDGPU_HW_IP_GFX
			|| query->query_hw_ip.type == AMDGPU_HW_IP_COMPUTE)
			count = 1;
		return r5_copy_out(out, query->return_size, &count, sizeof(count));
	}
	if (query->query == AMDGPU_INFO_READ_MMR_REG) {
		uint32_t offset = query->read_mmr_reg.dword_offset;
		if (offset == 0x263eu)
			offset = 0x063eu;
		if (offset != 0x063eu || query->read_mmr_reg.count != 1)
			return r5_fail(EPERM);
		r5_gpu_read_reg reg;
		memset(&reg, 0, sizeof(reg));
		reg.size = (uint32_t)sizeof(reg);
		reg.version = 1;
		reg.offset = offset;
		if (r5_call(fd, R5_GPU_READ_REG, &reg, sizeof(reg)) != 0)
			return -1;
		return r5_copy_out(out, query->return_size, &reg.value, sizeof(reg.value));
	}
	if (query->query == AMDGPU_INFO_VRAM_GTT) {
		struct drm_amdgpu_info_vram_gtt heap;
		memset(&heap, 0, sizeof(heap));
		heap.vram_size = info.vram_bytes;
		heap.vram_cpu_accessible_size = info.vram_bytes;
		heap.gtt_size = info.gtt_bytes;
		return r5_copy_out(out, query->return_size, &heap, sizeof(heap));
	}
	if (query->query == AMDGPU_INFO_MEMORY) {
		struct drm_amdgpu_memory_info memory;
		uint64_t vram = info.vram_bytes;
		uint64_t gtt = info.gtt_bytes;
		uint64_t cap = 64ull * 1024ull * 1024ull;
		memset(&memory, 0, sizeof(memory));
		memory.vram.total_heap_size = vram;
		memory.vram.usable_heap_size = vram;
		memory.vram.max_allocation = vram < cap ? vram : cap;
		memory.cpu_accessible_vram = memory.vram;
		memory.gtt.total_heap_size = gtt;
		memory.gtt.usable_heap_size = gtt;
		memory.gtt.max_allocation = gtt < cap ? gtt : cap;
		return r5_copy_out(out, query->return_size, &memory, sizeof(memory));
	}
	return r5_fail(ENOSYS);
}


static uint32_t
r5_domain(uint64_t domains)
{
	if ((domains & AMDGPU_GEM_DOMAIN_VRAM) != 0)
		return R5_GPU_DOMAIN_VRAM;
	return R5_GPU_DOMAIN_GTT;
}


static int
r5_gem_create(int fd, union drm_amdgpu_gem_create *gem)
{
	r5_gpu_bo bo;
	memset(&bo, 0, sizeof(bo));
	bo.size = (uint32_t)sizeof(bo);
	bo.version = 1;
	bo.domain = r5_domain(gem->in.domains);
	bo.bytes = gem->in.bo_size;
	if (r5_call(fd, R5_GPU_BO_CREATE, &bo, sizeof(bo)) != 0)
		return -1;
	gem->out.handle = bo.handle;
	return 0;
}


static int
r5_gem_close(int fd, struct drm_gem_close *close)
{
	r5_gpu_bo bo;
	memset(&bo, 0, sizeof(bo));
	bo.size = (uint32_t)sizeof(bo);
	bo.version = 1;
	bo.handle = close->handle;
	if (r5_call(fd, R5_GPU_BO_FREE, &bo, sizeof(bo)) != 0)
		return -1;
	return 0;
}


static int
r5_ctx(int fd, union drm_amdgpu_ctx *ctx)
{
	r5_gpu_ctx call;
	memset(&call, 0, sizeof(call));
	call.size = (uint32_t)sizeof(call);
	call.version = 1;
	if (ctx->in.op == AMDGPU_CTX_OP_ALLOC_CTX) {
		if (r5_call(fd, R5_GPU_CTX_CREATE, &call, sizeof(call)) != 0)
			return -1;
		ctx->out.alloc.ctx_id = call.context;
		return 0;
	}
	if (ctx->in.op == AMDGPU_CTX_OP_FREE_CTX) {
		call.context = ctx->in.ctx_id;
		if (r5_call(fd, R5_GPU_CTX_DESTROY, &call, sizeof(call)) != 0)
			return -1;
		return 0;
	}
	return r5_fail(ENOSYS);
}


static uint32_t
r5_va_flags(uint32_t flags)
{
	uint32_t out = 0;
	if ((flags & AMDGPU_VM_PAGE_READABLE) != 0)
		out |= R5_GPU_VA_READ;
	if ((flags & AMDGPU_VM_PAGE_WRITEABLE) != 0)
		out |= R5_GPU_VA_WRITE;
	if ((flags & AMDGPU_VM_PAGE_EXECUTABLE) != 0)
		out |= R5_GPU_VA_EXEC;
	if (out == 0)
		out = R5_GPU_VA_READ | R5_GPU_VA_WRITE;
	return out;
}


static int
r5_gem_va(int fd, struct drm_amdgpu_gem_va *va)
{
	r5_gpu_va call;
	memset(&call, 0, sizeof(call));
	call.size = (uint32_t)sizeof(call);
	call.version = 1;
	/* libdrm 2.4.124 drm_amdgpu_gem_va has no context field. */
	call.context = 1;
	call.handle = va->handle;
	call.gpu_va = va->va_address;
	call.bo_offset = va->offset_in_bo;
	call.map_bytes = va->map_size;
	call.flags = r5_va_flags(va->flags);
	unsigned long op = 0;
	if (va->operation == AMDGPU_VA_OP_MAP)
		op = R5_GPU_VA_MAP;
	else if (va->operation == AMDGPU_VA_OP_UNMAP)
		op = R5_GPU_VA_UNMAP;
	else
		return r5_fail(ENOSYS);
	return r5_call(fd, op, &call, sizeof(call));
}


static int
r5_cs(int fd, union drm_amdgpu_cs *cs)
{
	uint64_t *chunks = (uint64_t *)(uintptr_t)cs->in.chunks;
	if (cs->in.num_chunks == 0 || chunks == NULL)
		return r5_fail(EINVAL);
	r5_gpu_ib ibs[8];
	r5_gpu_cs_sync waits[8];
	r5_gpu_cs_sync signals[8];
	uint32_t ibCount = 0;
	uint32_t waitCount = 0;
	uint32_t signalCount = 0;
	memset(ibs, 0, sizeof(ibs));
	memset(waits, 0, sizeof(waits));
	memset(signals, 0, sizeof(signals));
	for (uint32_t i = 0; i < cs->in.num_chunks; i++) {
		struct drm_amdgpu_cs_chunk *chunk =
			(struct drm_amdgpu_cs_chunk *)(uintptr_t)chunks[i];
		if (chunk == NULL)
			return r5_fail(EINVAL);
		if (chunk->chunk_id == AMDGPU_CHUNK_ID_IB) {
			struct drm_amdgpu_cs_chunk_ib *ib =
				(struct drm_amdgpu_cs_chunk_ib *)(uintptr_t)chunk->chunk_data;
			if (ib == NULL || ibCount >= 8 || (ib->ib_bytes & 3u) != 0)
				return r5_fail(EINVAL);
			if (ib->ip_type != AMDGPU_HW_IP_GFX)
				return r5_fail(EINVAL);
			if (ib->flags != 0)
				return r5_fail(EINVAL);
			ibs[ibCount].gpu_va = ib->va_start;
			ibs[ibCount].dwords = ib->ib_bytes / 4u;
			ibs[ibCount].flags = 0;
			ibCount++;
		} else if (chunk->chunk_id == AMDGPU_CHUNK_ID_SYNCOBJ_IN
			|| chunk->chunk_id == AMDGPU_CHUNK_ID_SYNCOBJ_OUT) {
			uint32_t bytes = chunk->length_dw * 4u;
			uint32_t count = bytes / (uint32_t)sizeof(struct drm_amdgpu_cs_chunk_sem);
			struct drm_amdgpu_cs_chunk_sem *list =
				(struct drm_amdgpu_cs_chunk_sem *)(uintptr_t)chunk->chunk_data;
			int signal = chunk->chunk_id == AMDGPU_CHUNK_ID_SYNCOBJ_OUT;
			if (bytes == 0)
				continue;
			if (list == NULL || (bytes % sizeof(struct drm_amdgpu_cs_chunk_sem)) != 0)
				return r5_fail(EINVAL);
			for (uint32_t n = 0; n < count; n++) {
				r5_gpu_cs_sync *slot;
				if (signal) {
					if (signalCount >= 8)
						return r5_fail(EINVAL);
					slot = &signals[signalCount++];
				} else {
					if (waitCount >= 8)
						return r5_fail(EINVAL);
					slot = &waits[waitCount++];
				}
				slot->handle = list[n].handle;
				slot->flags = R5_GPU_CS_SYNC_BINARY;
				slot->point = 0;
			}
		} else if (chunk->chunk_id == AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT
			|| chunk->chunk_id == AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL) {
			uint32_t bytes = chunk->length_dw * 4u;
			uint32_t count = bytes / (uint32_t)sizeof(struct drm_amdgpu_cs_chunk_syncobj);
			struct drm_amdgpu_cs_chunk_syncobj *list =
				(struct drm_amdgpu_cs_chunk_syncobj *)(uintptr_t)chunk->chunk_data;
			int signal = chunk->chunk_id == AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL;
			if (bytes == 0)
				continue;
			if (list == NULL || (bytes % sizeof(struct drm_amdgpu_cs_chunk_syncobj)) != 0)
				return r5_fail(EINVAL);
			for (uint32_t n = 0; n < count; n++) {
				r5_gpu_cs_sync *slot;
				if (signal) {
					if (signalCount >= 8)
						return r5_fail(EINVAL);
					slot = &signals[signalCount++];
				} else {
					if (waitCount >= 8)
						return r5_fail(EINVAL);
					slot = &waits[waitCount++];
				}
				slot->handle = list[n].handle;
				slot->flags = 0;
				slot->point = list[n].point;
			}
		} else if (chunk->chunk_id == AMDGPU_CHUNK_ID_DEPENDENCIES) {
			uint32_t bytes = chunk->length_dw * 4u;
			uint32_t count = bytes / (uint32_t)sizeof(struct drm_amdgpu_cs_chunk_dep);
			struct drm_amdgpu_cs_chunk_dep *list =
				(struct drm_amdgpu_cs_chunk_dep *)(uintptr_t)chunk->chunk_data;
			if (list == NULL || (bytes % sizeof(struct drm_amdgpu_cs_chunk_dep)) != 0)
				return r5_fail(EINVAL);
			for (uint32_t n = 0; n < count; n++) {
				r5_gpu_fence fence;
				memset(&fence, 0, sizeof(fence));
				fence.size = (uint32_t)sizeof(fence);
				fence.version = 1;
				fence.sequence = list[n].handle;
				fence.timeout_us = 5000000;
				if (r5_call(fd, R5_GPU_FENCE_WAIT, &fence, sizeof(fence)) != 0)
					return -1;
			}
		} else if (chunk->chunk_id == AMDGPU_CHUNK_ID_FENCE
			|| chunk->chunk_id == AMDGPU_CHUNK_ID_BO_HANDLES)
			continue;
		else
			return r5_fail(ENOSYS);
	}
	if (ibCount == 0)
		return r5_fail(EINVAL);
	r5_gpu_cs call;
	memset(&call, 0, sizeof(call));
	call.size = (uint32_t)sizeof(call);
	call.version = 1;
	call.context = cs->in.ctx_id;
	call.ring = 0;
	call.ib_count = ibCount;
	call.wait_count = waitCount;
	call.signal_count = signalCount;
	call.ibs = (uint64_t)(uintptr_t)ibs;
	call.waits = (uint64_t)(uintptr_t)waits;
	call.signals = (uint64_t)(uintptr_t)signals;
	if (r5_call(fd, R5_GPU_CS_SUBMIT, &call, sizeof(call)) != 0)
		return -1;
	cs->out.handle = call.sequence;
	return 0;
}


static uint64_t
r5_timeout_us(int64_t timeout_nsec)
{
	struct timespec now;
	int64_t left = timeout_nsec;
	if (timeout_nsec <= 0)
		return 0;
	if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
		int64_t clock = (int64_t)now.tv_sec * 1000000000ll + now.tv_nsec;
		if (timeout_nsec > clock)
			left = timeout_nsec - clock;
		else
			left = 0;
	}
	if (left < 0)
		left = 0;
	return (uint64_t)(left / 1000);
}


static int
r5_sync_call(int fd, unsigned long op, uint32_t handle, uint32_t flags,
	uint64_t point, uint64_t timeout_us, uint64_t *outPoint)
{
	r5_gpu_syncobj sync;
	memset(&sync, 0, sizeof(sync));
	sync.size = (uint32_t)sizeof(sync);
	sync.version = 1;
	sync.handle = handle;
	sync.flags = flags;
	sync.point = point;
	sync.timeout_us = timeout_us;
	if (r5_call(fd, op, &sync, sizeof(sync)) != 0)
		return -1;
	if (outPoint != NULL)
		*outPoint = sync.point;
	if (op == R5_GPU_SYNCOBJ_CREATE && handle == 0)
		return (int)sync.handle;
	return 0;
}


static int
r5_sync_create(int fd, struct drm_syncobj_create *create)
{
	int handle = r5_sync_call(fd, R5_GPU_SYNCOBJ_CREATE, 0,
		(create->flags & DRM_SYNCOBJ_CREATE_SIGNALED) ? R5_GPU_SYNCOBJ_SIGNALED : 0,
		0, 0, NULL);
	if (handle < 0)
		return -1;
	create->handle = (uint32_t)handle;
	return 0;
}


static int
r5_sync_one(int fd, unsigned long op, uint32_t handle, uint64_t point,
	uint32_t flags, uint64_t timeout_us)
{
	return r5_sync_call(fd, op, handle, flags, point, timeout_us, NULL);
}


static int
r5_sync_wait(int fd, uint32_t *handles, uint64_t *points, uint32_t count,
	uint32_t drmFlags, int binary, int64_t timeout_nsec, uint32_t *first)
{
	r5_gpu_syncobj_wait wait;
	memset(&wait, 0, sizeof(wait));
	wait.size = (uint32_t)sizeof(wait);
	wait.version = 1;
	wait.count = count;
	wait.handles = (uint64_t)(uintptr_t)handles;
	wait.points = (uint64_t)(uintptr_t)points;
	wait.timeout_us = r5_timeout_us(timeout_nsec);
	if ((drmFlags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) != 0)
		wait.flags |= R5_GPU_WAIT_ALL;
	if ((drmFlags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE) != 0)
		wait.flags |= R5_GPU_WAIT_AVAILABLE;
	if ((drmFlags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT) != 0)
		wait.flags |= R5_GPU_WAIT_FOR_SUBMIT;
	if (binary)
		wait.flags |= R5_GPU_WAIT_BINARY;
	if (r5_call(fd, R5_GPU_SYNCOBJ_WAIT_LIST, &wait, sizeof(wait)) != 0) {
		if (errno == ETIMEDOUT
#if defined(__HAIKU__)
			|| errno == B_TIMED_OUT
#endif
			)
			errno = ETIME;
		return -1;
	}
	if (first != NULL)
		*first = wait.first_signaled;
	return 0;
}


static int
r5_get_cap(struct drm_get_cap *cap)
{
	cap->value = 0;
	if (cap->capability == DRM_CAP_SYNCOBJ || cap->capability == DRM_CAP_SYNCOBJ_TIMELINE)
		cap->value = 1;
	return 0;
}


static int
r5_wait_cs(int fd, union drm_amdgpu_wait_cs *wait)
{
	r5_gpu_fence fence;
	memset(&fence, 0, sizeof(fence));
	fence.size = (uint32_t)sizeof(fence);
	fence.version = 1;
	fence.sequence = wait->in.handle;
	fence.timeout_us = r5_timeout_us((int64_t)wait->in.timeout);
	if (r5_call(fd, R5_GPU_FENCE_WAIT, &fence, sizeof(fence)) != 0) {
		if (errno == ETIMEDOUT
#if defined(__HAIKU__)
			|| errno == B_TIMED_OUT
#endif
			) {
			wait->out.status = 1;
			return 0;
		}
		return -1;
	}
	wait->out.status = 0;
	return 0;
}


static const char *
r5_dev_dir(void)
{
#if defined(R5_HOST_TEST)
	const char *env = getenv("R5_GPU_DEV_DIR");
	if (env != NULL && env[0] != '\0')
		return env;
#endif
	return "/dev/gpu/radeon_hd";
}


static drmDevicePtr
r5_device_new(const char *path, const r5_gpu_info *info)
{
	size_t extra = DRM_NODE_MAX * (sizeof(void *) + 64);
	size_t bytes = sizeof(drmDevice) + extra + sizeof(drmPciBusInfo)
		+ sizeof(drmPciDeviceInfo);
	drmDevice *device = calloc(1, bytes);
	char *ptr;
	unsigned i;
	if (device == NULL)
		return NULL;
	ptr = (char *)device + sizeof(*device);
	device->nodes = (char **)ptr;
	ptr += DRM_NODE_MAX * sizeof(void *);
	for (i = 0; i < DRM_NODE_MAX; i++) {
		device->nodes[i] = ptr;
		ptr += 64;
	}
	snprintf(device->nodes[DRM_NODE_RENDER], 64, "%s", path);
	device->available_nodes = 1 << DRM_NODE_RENDER;
	device->bustype = DRM_BUS_PCI;
	device->businfo.pci = (drmPciBusInfoPtr)ptr;
	ptr += sizeof(drmPciBusInfo);
	device->deviceinfo.pci = (drmPciDeviceInfoPtr)ptr;
	device->businfo.pci->domain = 0;
	device->businfo.pci->bus = (uint8_t)info->bus;
	device->businfo.pci->dev = (uint8_t)info->dev;
	device->businfo.pci->func = (uint8_t)info->func;
	device->deviceinfo.pci->vendor_id = (uint16_t)info->vendor;
	device->deviceinfo.pci->device_id = (uint16_t)info->device;
	device->deviceinfo.pci->revision_id = (uint8_t)info->revision;
	device->deviceinfo.pci->subvendor_id = (uint16_t)info->subsystem_vendor;
	device->deviceinfo.pci->subdevice_id = (uint16_t)info->subsystem_device;
	return device;
}


static drmDevicePtr
r5_device_from_fd(int fd, const char *path)
{
	r5_gpu_info info;
	r5_gpu_amd_info amd;
	memset(&info, 0, sizeof(info));
	memset(&amd, 0, sizeof(amd));
	if (r5_info(fd, &info, &amd) != 0) {
		fprintf(stderr, "r5 libdrm: R5_GPU_GET_INFO failed: %s\n",
			strerror(errno));
		return NULL;
	}
	return r5_device_new(path, &info);
}


int
r5_drm_get_devices2(uint32_t flags, drmDevicePtr devices[], int max_devices)
{
	DIR *dir;
	struct dirent *dent;
	drmDevicePtr found[8];
	int count = 0;
	(void)flags;
	dir = opendir(r5_dev_dir());
	/* Linux libdrm returns 0 devices when /dev/dri is missing. A negative
	 * Haiku errno, negated again, is a large positive count, and RADV
	 * then walks devices[] slots that were never filled. */
	if (dir == NULL)
		return 0;
	while ((dent = readdir(dir)) != NULL && count < 8) {
		char path[512];
		const char *directory = r5_dev_dir();
		size_t dirLen = strlen(directory);
		size_t nameLen = strlen(dent->d_name);
		int node;
		drmDevicePtr device;
		if (dent->d_name[0] == '.')
			continue;
		if (dirLen + 1 + nameLen + 1 > sizeof(path))
			continue;
		memcpy(path, directory, dirLen);
		path[dirLen] = '/';
		memcpy(path + dirLen + 1, dent->d_name, nameLen + 1);
		node = open(path, O_RDWR);
		if (node < 0)
			continue;
		device = r5_device_from_fd(node, path);
		close(node);
		if (device == NULL)
			continue;
		found[count++] = device;
	}
	closedir(dir);
	if (devices == NULL) {
		int i;
		for (i = 0; i < count; i++)
			drmFreeDevice(&found[i]);
		return count;
	}
	{
		int keep = count < max_devices ? count : max_devices;
		int i;
		for (i = 0; i < count; i++) {
			if (i < keep)
				devices[i] = found[i];
			else
				drmFreeDevice(&found[i]);
		}
		return keep;
	}
}


int
r5_drm_get_device2(int fd, uint32_t flags, drmDevicePtr *device)
{
	DIR *dir;
	struct dirent *dent;
	struct stat mine;
	(void)flags;
	if (device == NULL || fstat(fd, &mine) != 0)
		return r5_neg(EINVAL);
	dir = opendir(r5_dev_dir());
	if (dir == NULL)
		return r5_neg(errno);
	while ((dent = readdir(dir)) != NULL) {
		char path[512];
		const char *directory = r5_dev_dir();
		size_t dirLen = strlen(directory);
		size_t nameLen = strlen(dent->d_name);
		struct stat other;
		int node;
		if (dent->d_name[0] == '.')
			continue;
		if (dirLen + 1 + nameLen + 1 > sizeof(path))
			continue;
		memcpy(path, directory, dirLen);
		path[dirLen] = '/';
		memcpy(path + dirLen + 1, dent->d_name, nameLen + 1);
		if (stat(path, &other) != 0)
			continue;
		if (other.st_dev != mine.st_dev || other.st_ino != mine.st_ino)
			continue;
		node = open(path, O_RDWR);
		if (node < 0)
			continue;
		*device = r5_device_from_fd(node, path);
		close(node);
		closedir(dir);
		return *device == NULL ? r5_neg(errno) : 0;
	}
	closedir(dir);
	return r5_neg(ENODEV);
}


int
r5_drm_ioctl(int fd, unsigned long request, void *arg, size_t size)
{
	unsigned nr = R5_IOC_NR(request);
	(void)size;
	if (arg == NULL)
		return r5_fail(EINVAL);
	if (nr == R5_IOC_NR(DRM_IOCTL_VERSION))
		return r5_version(arg);
	if (nr == R5_IOC_NR(DRM_IOCTL_GET_CAP))
		return r5_get_cap(arg);
	if (nr == R5_IOC_NR(DRM_IOCTL_GET_CLIENT)) {
		struct drm_client *client = arg;
		client->auth = 0;
		return 0;
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_GEM_CLOSE))
		return r5_gem_close(fd, arg);
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_CREATE))
		return r5_sync_create(fd, arg);
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_DESTROY)) {
		struct drm_syncobj_destroy *destroy = arg;
		return r5_sync_one(fd, R5_GPU_SYNCOBJ_DESTROY, destroy->handle, 0, 0, 0);
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_RESET) || nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_SIGNAL)) {
		struct drm_syncobj_array *list = arg;
		uint32_t *handles = (uint32_t *)(uintptr_t)list->handles;
		uint32_t i;
		unsigned long op = nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_RESET)
			? R5_GPU_SYNCOBJ_RESET : R5_GPU_SYNCOBJ_SIGNAL;
		if (handles == NULL)
			return r5_fail(EINVAL);
		for (i = 0; i < list->count_handles; i++) {
			uint32_t flags = op == R5_GPU_SYNCOBJ_SIGNAL ? R5_GPU_SYNCOBJ_BINARY : 0;
			if (r5_sync_one(fd, op, handles[i], 0, flags, 0) != 0)
				return -1;
		}
		return 0;
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_WAIT)) {
		struct drm_syncobj_wait *wait = arg;
		uint32_t *handles = (uint32_t *)(uintptr_t)wait->handles;
		if (handles == NULL)
			return r5_fail(EINVAL);
		if (r5_sync_wait(fd, handles, NULL, wait->count_handles, wait->flags,
			1, wait->timeout_nsec, &wait->first_signaled) != 0)
			return -1;
		return 0;
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT)) {
		struct drm_syncobj_timeline_wait *wait = arg;
		uint32_t *handles = (uint32_t *)(uintptr_t)wait->handles;
		uint64_t *points = (uint64_t *)(uintptr_t)wait->points;
		if (handles == NULL || points == NULL)
			return r5_fail(EINVAL);
		if (r5_sync_wait(fd, handles, points, wait->count_handles, wait->flags,
			0, wait->timeout_nsec, &wait->first_signaled) != 0)
			return -1;
		return 0;
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL)) {
		struct drm_syncobj_timeline_array *list = arg;
		uint32_t *handles = (uint32_t *)(uintptr_t)list->handles;
		uint64_t *points = (uint64_t *)(uintptr_t)list->points;
		uint32_t i;
		if (handles == NULL || points == NULL)
			return r5_fail(EINVAL);
		for (i = 0; i < list->count_handles; i++) {
			if (r5_sync_one(fd, R5_GPU_SYNCOBJ_SIGNAL, handles[i], points[i], 0, 0) != 0)
				return -1;
		}
		return 0;
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_QUERY)) {
		struct drm_syncobj_timeline_array *list = arg;
		uint32_t *handles = (uint32_t *)(uintptr_t)list->handles;
		uint64_t *points = (uint64_t *)(uintptr_t)list->points;
		uint32_t i;
		if (handles == NULL || points == NULL)
			return r5_fail(EINVAL);
		for (i = 0; i < list->count_handles; i++) {
			if (r5_sync_call(fd, R5_GPU_SYNCOBJ_QUERY, handles[i], 0, 0, 0,
				&points[i]) != 0)
				return -1;
		}
		return 0;
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD)) {
		r5_unknown(nr, "DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD");
		return r5_fail(ENOSYS);
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE)) {
		r5_unknown(nr, "DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE");
		return r5_fail(ENOSYS);
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_SYNCOBJ_TRANSFER)) {
		r5_unknown(nr, "DRM_IOCTL_SYNCOBJ_TRANSFER");
		return r5_fail(ENOSYS);
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_PRIME_HANDLE_TO_FD)
		|| nr == R5_IOC_NR(DRM_IOCTL_PRIME_FD_TO_HANDLE)) {
		r5_unknown(nr, "DRM_IOCTL_PRIME");
		return r5_fail(ENOSYS);
	}
	if (nr == R5_IOC_NR(DRM_IOCTL_AMDGPU_INFO))
		return r5_amdgpu_info(fd, arg);
	if (nr == R5_IOC_NR(DRM_IOCTL_AMDGPU_GEM_CREATE))
		return r5_gem_create(fd, arg);
	if (nr == R5_IOC_NR(DRM_IOCTL_AMDGPU_CTX))
		return r5_ctx(fd, arg);
	if (nr == R5_IOC_NR(DRM_IOCTL_AMDGPU_GEM_VA))
		return r5_gem_va(fd, arg);
	if (nr == R5_IOC_NR(DRM_IOCTL_AMDGPU_CS))
		return r5_cs(fd, arg);
	if (nr == R5_IOC_NR(DRM_IOCTL_AMDGPU_WAIT_CS))
		return r5_wait_cs(fd, arg);
	r5_unknown(nr, NULL);
	return r5_fail(ENOSYS);
}


drm_public int
r5_bo_cpu_map(int fd, unsigned int handle, void **cpu, int *area)
{
	r5_gpu_bo bo;
	memset(&bo, 0, sizeof(bo));
	bo.size = (uint32_t)sizeof(bo);
	bo.version = 1;
	bo.handle = handle;
	if (r5_call(fd, R5_GPU_BO_MAP, &bo, sizeof(bo)) != 0)
		return r5_neg(errno);
	if (bo.area < 0)
		return r5_neg(ENODEV);
	void *address = r5_area_address(bo.area);
	if (address == NULL)
		return r5_neg(ENOMEM);
	if (area != NULL)
		*area = bo.area;
	*cpu = address;
	return 0;
}


drm_public int
r5_bo_cpu_unmap(int fd, int area)
{
	int ret;
	(void)fd;
	ret = r5_area_destroy(area);
	return ret == 0 ? 0 : r5_neg(errno);
}
