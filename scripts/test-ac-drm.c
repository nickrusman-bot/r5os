/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host replay of Mesa's ac_linux_drm.c through libdrm's drmIoctl.
 */
#include "ac_linux_drm.h"

#include <amdgpu_drm.h>
#include <xf86drm.h>

#include <errno.h>

#include <stdio.h>
#include <string.h>


int
drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device)
{
	(void)fd;
	(void)flags;
	(void)device;
	errno = ENODEV;
	return -1;
}


int
ac_drm_replay(int fd)
{
	uint32_t major = 0;
	uint32_t minor = 0;
	ac_drm_device *device = NULL;
	int result = ac_drm_device_initialize(fd, false, &major, &minor, &device);
	if (result != 0 || device == NULL) {
		fprintf(stderr, "initialize %d\n", result);
		return 1;
	}

	struct amdgpu_gpu_info info;
	memset(&info, 0, sizeof(info));
	result = ac_drm_query_gpu_info(device, &info);
	if (result != 0) {
		fprintf(stderr, "query_gpu_info %d\n", result);
		return 1;
	}
	if (info.family_id != AMDGPU_FAMILY_RV || info.cu_active_number != 7
		|| info.asic_id != 0x15e7) {
		fprintf(stderr, "gpu family %u cu %u id %x\n", info.family_id,
			info.cu_active_number, info.asic_id);
		return 1;
	}

	struct drm_amdgpu_info_device deviceInfo;
	memset(&deviceInfo, 0, sizeof(deviceInfo));
	result = ac_drm_query_info(device, AMDGPU_INFO_DEV_INFO,
		sizeof(deviceInfo), &deviceInfo);
	if (result != 0 || deviceInfo.cu_active_number != 7
		|| deviceInfo.family != AMDGPU_FAMILY_RV) {
		fprintf(stderr, "dev_info %d cu %u family %u\n", result,
			deviceInfo.cu_active_number, deviceInfo.family);
		return 1;
	}

	uint32_t version = 0;
	uint32_t feature = 0;
	result = ac_drm_query_firmware_version(device, AMDGPU_INFO_FW_GFX_ME,
		0, 0, &version, &feature);
	if (result != 0 || version != 0x11) {
		fprintf(stderr, "me %d %u\n", result, version);
		return 1;
	}
	result = ac_drm_query_firmware_version(device, AMDGPU_INFO_FW_GFX_PFP,
		0, 0, &version, &feature);
	if (result != 0 || version != 0xc5) {
		fprintf(stderr, "pfp %d %u\n", result, version);
		return 1;
	}
	result = ac_drm_query_firmware_version(device, AMDGPU_INFO_FW_GFX_MEC,
		0, 0, &version, &feature);
	if (result != 0 || version != 0x1e3) {
		fprintf(stderr, "mec %d %u\n", result, version);
		return 1;
	}

	struct amdgpu_bo_alloc_request request;
	memset(&request, 0, sizeof(request));
	request.alloc_size = 4096;
	request.phys_alignment = 4096;
	request.preferred_heap = AMDGPU_GEM_DOMAIN_GTT;
	ac_drm_bo bo;
	memset(&bo, 0, sizeof(bo));
	result = ac_drm_bo_alloc(device, &request, &bo);
	if (result != 0) {
		fprintf(stderr, "bo_alloc %d\n", result);
		return 1;
	}
	void *cpu = NULL;
	result = ac_drm_bo_cpu_map(device, bo, &cpu);
	if (result != 0 || cpu == NULL) {
		fprintf(stderr, "bo_cpu_map %d\n", result);
		return 1;
	}
	*(uint32_t *)cpu = 0x5a5a5a5au;
	if (*(uint32_t *)cpu != 0x5a5a5a5au) {
		fprintf(stderr, "bo_cpu_map read\n");
		return 1;
	}
	result = ac_drm_bo_cpu_unmap(device, bo);
	if (result != 0) {
		fprintf(stderr, "bo_cpu_unmap %d\n", result);
		return 1;
	}

	printf("ok ac drm family RV cu 7 firmware map\n");
	return 0;
}
