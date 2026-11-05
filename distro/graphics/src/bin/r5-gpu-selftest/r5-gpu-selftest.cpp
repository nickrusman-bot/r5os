/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Writes one result file after Sign in. The launch job runs it only when
 * the radeon_hd driver settings say gpu_selftest.
 */


#include "r5_gpu.h"

#include <FindDirectory.h>
#include <Path.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>


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
		fprintf(out, "stage vulkaninfo: not available, no supported GPU\n");
	} else {
		uint8_t bytes[sizeof(r5_gpu_info) + sizeof(r5_gpu_amd_info)];
		memset(bytes, 0, sizeof(bytes));
		if (ioctl(fd, R5_GPU_GET_INFO, bytes, sizeof(bytes)) != 0) {
			fprintf(out, "stage render node: GET_INFO failed\n");
		} else {
			r5_gpu_info* info = (r5_gpu_info*)bytes;
			r5_gpu_amd_info* amd = (r5_gpu_amd_info*)(bytes + sizeof(r5_gpu_info));
			fprintf(out, "stage render node: api %u.%u device %04x cu %u\n",
				info->api_major, info->api_minor, info->device,
				amd->device.cu_active_number);
		}
		close(fd);
		write_command(out, "vulkaninfo",
			"/bin/vulkaninfo --summary 2>&1");
	}
	write_command(out, "VulkanTeapot",
		"/bin/VulkanTeapot --selftest 2>&1");
	fclose(out);
	return 0;
}
