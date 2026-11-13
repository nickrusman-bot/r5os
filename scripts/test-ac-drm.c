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
#include <unistd.h>

/* distro/graphics/libdrm-r5/r5_translate.h (M11 step 3b). */
int r5_drm_fd_to_token(int fd, uint8_t token[16], uint32_t *id);
int r5_drm_token_to_fd(int device_fd, const uint8_t token[16],
	int32_t client_team, int *fd, uint32_t *id);


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
		|| deviceInfo.family != AMDGPU_FAMILY_RV
		|| deviceInfo.num_rb_pipes == 0
		|| deviceInfo.enabled_rb_pipes_mask == 0
		|| deviceInfo.num_tcc_blocks == 0
		|| deviceInfo.max_engine_clock == 0
		|| deviceInfo.max_memory_clock == 0
		|| deviceInfo.num_shader_visible_vgprs == 0
		|| deviceInfo.ce_ram_size == 0
		|| deviceInfo.vram_type == 0
		|| deviceInfo.vram_bit_width == 0
		|| deviceInfo.wave_front_size == 0
		|| deviceInfo.num_hw_gfx_contexts == 0
		|| info.gb_addr_cfg == 0) {
		fprintf(stderr, "dev_info %d cu %u family %u rb %u tcc %u "
			"clk %llu gb %x\n", result,
			deviceInfo.cu_active_number, deviceInfo.family,
			deviceInfo.num_rb_pipes, deviceInfo.num_tcc_blocks,
			(unsigned long long)deviceInfo.max_engine_clock,
			info.gb_addr_cfg);
		return 1;
	}
	if (deviceInfo.pa_sc_tile_steering_override != 0
		|| deviceInfo.tcc_disabled_mask != 0
		|| deviceInfo.prim_buf_gpu_addr != 0
		|| deviceInfo.tcp_cache_size != 0
		|| deviceInfo.shadow_size != 0
		|| deviceInfo.vce_harvest_config != 0
		|| deviceInfo.enabled_rb_pipes_mask_hi != 0) {
		fprintf(stderr, "dev_info zero field set\n");
		return 1;
	}
	{
		uint64_t reset = 1;
		result = ac_drm_cs_query_reset_state2(device, 1, &reset);
		if (result != 0 || reset != 0) {
			fprintf(stderr, "query2 %d flags %llx\n", result,
				(unsigned long long)reset);
			return 1;
		}
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

	/* M11 step 3b: RADV's import path (radv_amdgpu_winsys_bo_from_fd):
	 * ac_drm_bo_import, the KMS handle, then ac_drm_bo_query_info, which
	 * sends GEM_METADATA and GEM_OP. */
	{
		uint32_t exported = 0;
		uint8_t token[16];
		int fd = -1;
		uint32_t id = 0;
		struct ac_drm_bo_import_result imported;
		struct amdgpu_bo_info info;
		uint32_t kms = 0;
		memset(&imported, 0, sizeof(imported));
		memset(&info, 0, sizeof(info));
		if (ac_drm_bo_export(device, bo, amdgpu_bo_handle_type_dma_buf_fd,
				&exported) != 0
			|| r5_drm_fd_to_token((int)exported, token, &id) != 0) {
			fprintf(stderr, "share export\n");
			return 1;
		}
		close((int)exported);
		if (r5_drm_token_to_fd(-1, token, 77, &fd, &id) != 0
			|| ac_drm_bo_import(device, amdgpu_bo_handle_type_dma_buf_fd,
				(uint32_t)fd, &imported) != 0) {
			fprintf(stderr, "share import\n");
			return 1;
		}
		close(fd);
		if (ac_drm_bo_export(device, imported.bo, amdgpu_bo_handle_type_kms,
				&kms) != 0
			|| ac_drm_bo_query_info(device, kms, &info) != 0
			|| imported.alloc_size != 4096 || info.alloc_size != 4096
			|| info.preferred_heap != AMDGPU_GEM_DOMAIN_GTT
			|| info.metadata.size_metadata != 0) {
			fprintf(stderr, "share query size %llu %llu heap %u\n",
				(unsigned long long)imported.alloc_size,
				(unsigned long long)info.alloc_size,
				(unsigned)info.preferred_heap);
			return 1;
		}
		ac_drm_bo_free(device, imported.bo);
	}

	printf("ok ac drm family RV cu 7 firmware map share\n");
	return 0;
}
