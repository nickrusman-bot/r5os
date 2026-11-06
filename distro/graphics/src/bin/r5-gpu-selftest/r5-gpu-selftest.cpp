/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Login starts this after Deskbar. The result file is written even when
 * the render node is absent.
 */


#include "r5_gpu.h"

#include <FindDirectory.h>
#include <Path.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>


struct r5_bo_alloc {
	uint64_t	alloc_size;
	uint64_t	phys_alignment;
	uint32_t	preferred_heap;
	uint64_t	flags;
};

struct r5_cs_ib {
	uint64_t	flags;
	uint64_t	ib_mc_address;
	uint32_t	size;
};

struct r5_cs_fence_info {
	void*		handle;
	uint64_t	offset;
};

struct r5_cs_request {
	uint64_t	flags;
	unsigned	ip_type;
	unsigned	ip_instance;
	uint32_t	ring;
	void*		resources;
	uint32_t	number_of_dependencies;
	void*		dependencies;
	uint32_t	number_of_ibs;
	r5_cs_ib*	ibs;
	uint64_t	seq_no;
	r5_cs_fence_info	fence_info;
};

struct r5_cs_fence {
	void*		context;
	uint32_t	ip_type;
	uint32_t	ip_instance;
	uint32_t	ring;
	uint64_t	fence;
};


static void*
symbol(void* library, const char* name)
{
	return dlsym(library, name);
}


static void
stage_bo(FILE* out, uint64_t lowVa, uint64_t highVa)
{
	fprintf(out, "stage bo\n");
	void* library = dlopen("libdrm_amdgpu.so.1", RTLD_NOW);
	if (library == NULL) {
		fprintf(out, "not available, %s\n", dlerror());
		return;
	}
	typedef int (*init_fn)(int, uint32_t*, uint32_t*, void**);
	typedef int (*deinit_fn)(void*);
	typedef int (*bo_alloc_fn)(void*, r5_bo_alloc*, void**);
	typedef int (*bo_free_fn)(void*);
	typedef int (*va_op_fn)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint32_t);
	typedef int (*map_fn)(void*, void**);
	typedef int (*unmap_fn)(void*);
	typedef int (*ctx_fn)(void*, void**);
	typedef int (*ctx_free_fn)(void*);
	typedef int (*submit_fn)(void*, uint64_t, r5_cs_request*, uint32_t);
	typedef int (*fence_fn)(r5_cs_fence*, uint64_t, uint64_t, uint32_t*);
	init_fn doInit = (init_fn)symbol(library, "amdgpu_device_initialize");
	deinit_fn doDeinit = (deinit_fn)symbol(library, "amdgpu_device_deinitialize");
	bo_alloc_fn doAlloc = (bo_alloc_fn)symbol(library, "amdgpu_bo_alloc");
	bo_free_fn doFree = (bo_free_fn)symbol(library, "amdgpu_bo_free");
	va_op_fn doVa = (va_op_fn)symbol(library, "amdgpu_bo_va_op");
	map_fn doMap = (map_fn)symbol(library, "amdgpu_bo_cpu_map");
	unmap_fn doUnmap = (unmap_fn)symbol(library, "amdgpu_bo_cpu_unmap");
	ctx_fn doCtx = (ctx_fn)symbol(library, "amdgpu_cs_ctx_create");
	ctx_free_fn doCtxFree = (ctx_free_fn)symbol(library, "amdgpu_cs_ctx_free");
	submit_fn doSubmit = (submit_fn)symbol(library, "amdgpu_cs_submit");
	fence_fn doFence = (fence_fn)symbol(library, "amdgpu_cs_query_fence_status");
	if (doInit == NULL || doAlloc == NULL || doVa == NULL || doMap == NULL
		|| doSubmit == NULL || doFence == NULL) {
		fprintf(out, "not available, libdrm symbols\n");
		dlclose(library);
		return;
	}

	int saved = dup(2);
	fflush(out);
	if (saved >= 0)
		dup2(fileno(out), 2);

	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	uint32_t major = 0;
	uint32_t minor = 0;
	void* device = NULL;
	int ret = fd < 0 ? -1 : doInit(fd, &major, &minor, &device);
	fprintf(out, "device %d\n", ret);
	void* vram = NULL;
	void* gtt = NULL;
	void* context = NULL;
	if (ret == 0) {
		r5_bo_alloc request;
		memset(&request, 0, sizeof(request));
		request.alloc_size = 4096;
		request.preferred_heap = 4;
		ret = doAlloc(device, &request, &vram);
		fprintf(out, "GEM_CREATE vram %d\n", ret);
		request.preferred_heap = 2;
		ret = doAlloc(device, &request, &gtt);
		fprintf(out, "GEM_CREATE gtt %d\n", ret);
		if (vram != NULL) {
			ret = doVa(vram, 0, 4096, lowVa, 0, 1);
			fprintf(out, "GEM_VA low %d\n", ret);
		} else
			fprintf(out, "GEM_VA low not called\n");
		if (gtt != NULL) {
			ret = doVa(gtt, 0, 4096, highVa, 0, 1);
			fprintf(out, "GEM_VA high %d\n", ret);
		} else
			fprintf(out, "GEM_VA high not called\n");
		void* cpu = NULL;
		if (gtt != NULL && doMap != NULL) {
			ret = doMap(gtt, &cpu);
			fprintf(out, "GEM_MMAP %d\n", ret);
			if (ret == 0 && cpu != NULL) {
				((uint8_t*)cpu)[0] = 0x5a;
				fprintf(out, "cpu %02x\n", ((uint8_t*)cpu)[0]);
				((uint32_t*)cpu)[0] = 0x80000000u;
				if (doCtx != NULL)
					ret = doCtx(device, &context);
				else
					ret = -1;
				fprintf(out, "context %d\n", ret);
				if (context != NULL) {
					r5_cs_ib ib;
					memset(&ib, 0, sizeof(ib));
					ib.ib_mc_address = highVa;
					ib.size = 1;
					r5_cs_request requestCs;
					memset(&requestCs, 0, sizeof(requestCs));
					requestCs.number_of_ibs = 1;
					requestCs.ibs = &ib;
					ret = doSubmit(context, 0, &requestCs, 1);
					fprintf(out, "IB %d\n", ret);
					r5_cs_fence fence;
					memset(&fence, 0, sizeof(fence));
					fence.context = context;
					fence.fence = requestCs.seq_no;
					uint32_t expired = 0;
					int waited = doFence(&fence, 1000000000ull, 0, &expired);
					fprintf(out, "fence %d expired %u\n", waited, expired);
				} else
					fprintf(out, "IB not called\n");
			}
		} else
			fprintf(out, "GEM_MMAP not called\n");
		if (gtt != NULL) {
			ret = doVa(gtt, 0, 4096, highVa, 0, 2);
			fprintf(out, "GEM_VA unmap %d\n", ret);
			ret = doVa(gtt, 0, 4096, highVa, 0, 1);
			fprintf(out, "GEM_VA remap %d\n", ret);
		}
		if (cpu != NULL && doUnmap != NULL)
			doUnmap(gtt);
		if (context != NULL && doCtxFree != NULL)
			doCtxFree(context);
		if (vram != NULL && doFree != NULL)
			doFree(vram);
		if (gtt != NULL && doFree != NULL)
			doFree(gtt);
		if (doDeinit != NULL)
			doDeinit(device);
		fprintf(out, "close 0\n");
	}
	fflush(out);
	fflush(stderr);
	if (saved >= 0) {
		dup2(saved, 2);
		close(saved);
	}
	dlclose(library);
}


static void
write_command(FILE* out, const char* title, const char* command)
{
	fprintf(out, "stage %s\n", title);
	FILE* pipe = popen(command, "r");
	if (pipe == NULL) {
		fprintf(out, "not available, %s\n", strerror(errno));
		return;
	}
	char line[512];
	bool any = false;
	while (fgets(line, sizeof(line), pipe) != NULL) {
		fputs(line, out);
		any = true;
	}
	int status = pclose(pipe);
	if (!any)
		fprintf(out, "no output, status %d\n", status);
}


int
main()
{
	BPath cache;
	if (find_directory(B_USER_CACHE_DIRECTORY, &cache) != B_OK)
		return 1;
	cache.Append("r5-gpu-selftest.txt");
	FILE* out = fopen(cache.Path(), "w");
	if (out == NULL)
		return 1;

	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	if (fd < 0) {
		fprintf(out, "stage render node: not available, no supported GPU\n");
		fprintf(out, "stage bo: not available, no supported GPU\n");
		fprintf(out, "stage vulkaninfo: not available, no supported GPU\n");
	} else {
		uint8_t bytes[sizeof(r5_gpu_info) + sizeof(r5_gpu_amd_info)];
		memset(bytes, 0, sizeof(bytes));
		r5_gpu_info* info = (r5_gpu_info*)bytes;
		info->size = (uint32_t)sizeof(bytes);
		info->version = 1;
		if (ioctl(fd, R5_GPU_GET_INFO, bytes, sizeof(bytes)) != 0) {
			fprintf(out, "stage render node: GET_INFO failed\n");
		} else {
			r5_gpu_amd_info* amd = (r5_gpu_amd_info*)(bytes + sizeof(r5_gpu_info));
			fprintf(out, "stage render node: api %u.%u device %04x cu %u\n",
				info->api_major, info->api_minor, info->device,
				amd->device.cu_active_number);
			fprintf(out, "firmware me %u pfp %u mec %u\n",
				amd->firmware[0x04].ver, amd->firmware[0x05].ver,
				amd->firmware[0x08].ver);
		}
		close(fd);
		uint64_t lowVa = R5_GPU_VA_OFFSET;
		uint64_t highVa = R5_GPU_HIGH_VA_OFFSET;
		r5_gpu_info* reported = (r5_gpu_info*)bytes;
		if (reported->va_offset != 0)
			lowVa = reported->va_offset;
		if (reported->high_va_offset != 0)
			highVa = reported->high_va_offset;
		stage_bo(out, lowVa, highVa);
		write_command(out, "vulkaninfo",
			"/bin/vulkaninfo --summary 2>&1");
	}
	write_command(out, "VulkanTeapot",
		"/bin/VulkanTeapot --selftest 2>&1");
	fclose(out);
	return 0;
}
