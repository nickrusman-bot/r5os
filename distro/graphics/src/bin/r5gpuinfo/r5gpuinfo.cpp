/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Prints GET_INFO from the R5 GPU interface. The node is absent when the
 * machine has no radeon_hd render device.
 */


#include "r5_gpu.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>


int
main()
{
	const char* path = "/dev/gpu/radeon_hd/0";
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("r5gpuinfo: node absent\n");
		return 1;
	}
	uint8_t bytes[sizeof(r5_gpu_info) + sizeof(r5_gpu_amd_info)];
	memset(bytes, 0, sizeof(bytes));
	r5_gpu_info* info = (r5_gpu_info*)bytes;
	info->size = (uint32_t)sizeof(r5_gpu_info);
	info->version = 1;
	if (ioctl(fd, R5_GPU_GET_INFO, bytes, sizeof(bytes)) != 0) {
		printf("r5gpuinfo: GET_INFO failed\n");
		close(fd);
		return 1;
	}
	close(fd);
	printf("r5gpuinfo: api %u.%u vendor %04x device %04x rev %u cap %08x\n",
		info->api_major, info->api_minor, info->vendor, info->device,
		info->revision, info->capabilities);
	printf("r5gpuinfo: va %llx-%llx high %llx-%llx\n",
		(unsigned long long)info->va_offset,
		(unsigned long long)info->va_max,
		(unsigned long long)info->high_va_offset,
		(unsigned long long)info->high_va_max);
	if (sizeof(bytes) >= sizeof(r5_gpu_info) + sizeof(r5_gpu_amd_info)) {
		r5_gpu_amd_info* amd = (r5_gpu_amd_info*)(bytes + sizeof(r5_gpu_info));
		printf("r5gpuinfo: family %u cu %u external %x\n",
			amd->device.family, amd->device.cu_active_number,
			amd->device.external_rev);
	}
	return 0;
}
