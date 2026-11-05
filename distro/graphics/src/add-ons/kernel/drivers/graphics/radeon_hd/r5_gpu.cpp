/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Render node /dev/gpu/radeon_hd/<n>. The graphics node is unchanged.
 * CS_SUBMIT is refused unless the driver settings say gpu_render true.
 * VMID 1 page tables are not programmed yet, so a permitted submit
 * still returns B_UNSUPPORTED.
 */

#include "radeon_hd_private.h"
#include "r5_gpu.h"

#include <driver_settings.h>
#include <stdlib.h>
#include <string.h>


extern radeon_info* gDeviceInfo[];


/* gc_9_0_offset.h indexes. Byte address is (0x2000 + index) * 4.
 * INACTIVE_CUS is 16 bits at bit 16. Renoir's shader array has 8 CUs. */
static const uint32 kGcSegment = 0x2000;
static const uint32 kShaderArrayConfig = 0x026f;
static const uint32 kUserShaderArrayConfig = 0x0270;
static const uint32 kGbAddrConfig = 0x063e;
static const uint32 kInactiveShift = 16;
static const uint32 kInactiveMask = 0xffff0000u;
static const uint32 kCuPerSh = 8;


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


static status_t
gpu_open(const char* name, uint32 flags, void** cookie)
{
	(void)name;
	(void)flags;
	if (gDeviceInfo[0] == NULL)
		return B_ERROR;
	*cookie = gDeviceInfo[0];
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
	(void)cookie;
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
	radeon_info* info = (radeon_info*)cookie;
	if (op == R5_GPU_CS_SUBMIT) {
		if (!gpu_render_enabled())
			return B_NOT_ALLOWED;
		return B_UNSUPPORTED;
	}
	if (op != R5_GPU_GET_INFO)
		return B_UNSUPPORTED;
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
