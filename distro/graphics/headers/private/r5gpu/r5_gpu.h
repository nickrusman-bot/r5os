/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * R5 GPU interface, version 1.0. The calls are vendor-neutral.
 * AMD data lives only in the vendor block and in the command stream.
 * A shipped call does not change. A new call is added at the end.
 */

#ifndef R5_GPU_H
#define R5_GPU_H

#include <stdint.h>

#define R5_GPU_API_MAJOR 1u
#define R5_GPU_API_MINOR 0u

/* Haiku Drivers.h: B_DEVICE_OP_CODES_END is 9999. The numbers below are
 * that value plus 0x5000, then one fixed number per call. Never reuse one. */
#define R5_GPU_OP_BASE 30479u

enum {
	R5_GPU_GET_INFO = R5_GPU_OP_BASE + 0,
	R5_GPU_READ_REG = R5_GPU_OP_BASE + 1,
	R5_GPU_CTX_CREATE = R5_GPU_OP_BASE + 2,
	R5_GPU_CTX_DESTROY = R5_GPU_OP_BASE + 3,
	R5_GPU_BO_CREATE = R5_GPU_OP_BASE + 4,
	R5_GPU_BO_FREE = R5_GPU_OP_BASE + 5,
	R5_GPU_BO_MAP = R5_GPU_OP_BASE + 6,
	R5_GPU_VA_MAP = R5_GPU_OP_BASE + 7,
	R5_GPU_VA_UNMAP = R5_GPU_OP_BASE + 8,
	R5_GPU_CS_SUBMIT = R5_GPU_OP_BASE + 9,
	R5_GPU_FENCE_WAIT = R5_GPU_OP_BASE + 10,
	R5_GPU_SYNCOBJ_CREATE = R5_GPU_OP_BASE + 11,
	R5_GPU_SYNCOBJ_DESTROY = R5_GPU_OP_BASE + 12,
	R5_GPU_SYNCOBJ_WAIT = R5_GPU_OP_BASE + 13,
	R5_GPU_SYNCOBJ_SIGNAL = R5_GPU_OP_BASE + 14,
	R5_GPU_SYNCOBJ_RESET = R5_GPU_OP_BASE + 15,
	R5_GPU_SYNCOBJ_QUERY = R5_GPU_OP_BASE + 16,
	R5_GPU_BO_EXPORT = R5_GPU_OP_BASE + 17,
	R5_GPU_BO_IMPORT = R5_GPU_OP_BASE + 18
};

#define R5_GPU_CAP_RENDER 1u

#define R5_GPU_DOMAIN_VRAM 1u
#define R5_GPU_DOMAIN_GTT 2u

#define R5_GPU_VA_READ 1u
#define R5_GPU_VA_WRITE 2u
#define R5_GPU_VA_EXEC 4u

/* Linux v6.12 amdgpu_kms.c for a 48-bit GC 9.3 VM. See GRAPHICS.md. */
#define R5_GPU_VA_OFFSET 0x10000ull
#define R5_GPU_VA_MAX 0x800000000000ull
#define R5_GPU_HIGH_VA_OFFSET 0xffff800000000000ull
#define R5_GPU_HIGH_VA_MAX 0xffffffffffbfe000ull

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	api_major;
	uint32_t	api_minor;
	uint32_t	vendor;
	uint32_t	device;
	uint32_t	revision;
	uint32_t	capabilities;
	uint64_t	vram_bytes;
	uint64_t	gtt_bytes;
	uint64_t	va_offset;
	uint64_t	va_max;
	uint64_t	high_va_offset;
	uint64_t	high_va_max;
	uint64_t	timestamp_hz;
	uint32_t	vendor_block_size;
	uint32_t	vendor_block_version;
	uint32_t	bus;
	uint32_t	dev;
	uint32_t	func;
	uint32_t	subsystem_vendor;
	uint32_t	subsystem_device;
} r5_gpu_info;

/* AMDGPU_FAMILY_RV from libdrm amdgpu_drm.h. The vendor block below is that
 * header's drm_amdgpu_info_device, plus the firmware and HW_IP queries. */
#define R5_AMDGPU_FAMILY_RV 142u
#define R5_AMDGPU_IDS_FLAGS_FUSION 1u

#define R5_AMDGPU_HW_IP_GFX 0u
#define R5_AMDGPU_HW_IP_COMPUTE 1u
#define R5_AMDGPU_HW_IP_COUNT 10u
#define R5_AMDGPU_FW_COUNT 32u

/* Layout matches struct drm_amdgpu_info_device in libdrm 2.4.124. */
typedef struct {
	uint32_t	device_id;
	uint32_t	chip_rev;
	uint32_t	external_rev;
	uint32_t	pci_rev;
	uint32_t	family;
	uint32_t	num_shader_engines;
	uint32_t	num_shader_arrays_per_engine;
	uint32_t	gpu_counter_freq;
	uint64_t	max_engine_clock;
	uint64_t	max_memory_clock;
	uint32_t	cu_active_number;
	uint32_t	cu_ao_mask;
	uint32_t	cu_bitmap[4][4];
	uint32_t	enabled_rb_pipes_mask;
	uint32_t	num_rb_pipes;
	uint32_t	num_hw_gfx_contexts;
	uint32_t	pcie_gen;
	uint64_t	ids_flags;
	uint64_t	virtual_address_offset;
	uint64_t	virtual_address_max;
	uint32_t	virtual_address_alignment;
	uint32_t	pte_fragment_size;
	uint32_t	gart_page_size;
	uint32_t	ce_ram_size;
	uint32_t	vram_type;
	uint32_t	vram_bit_width;
	uint32_t	vce_harvest_config;
	uint32_t	gc_double_offchip_lds_buf;
	uint64_t	prim_buf_gpu_addr;
	uint64_t	pos_buf_gpu_addr;
	uint64_t	cntl_sb_buf_gpu_addr;
	uint64_t	param_buf_gpu_addr;
	uint32_t	prim_buf_size;
	uint32_t	pos_buf_size;
	uint32_t	cntl_sb_buf_size;
	uint32_t	param_buf_size;
	uint32_t	wave_front_size;
	uint32_t	num_shader_visible_vgprs;
	uint32_t	num_cu_per_sh;
	uint32_t	num_tcc_blocks;
	uint32_t	gs_vgt_table_depth;
	uint32_t	gs_prim_buffer_depth;
	uint32_t	max_gs_waves_per_vgt;
	uint32_t	pcie_num_lanes;
	uint32_t	cu_ao_bitmap[4][4];
	uint64_t	high_va_offset;
	uint64_t	high_va_max;
	uint32_t	pa_sc_tile_steering_override;
	uint64_t	tcc_disabled_mask;
	uint64_t	min_engine_clock;
	uint64_t	min_memory_clock;
	uint32_t	tcp_cache_size;
	uint32_t	num_sqc_per_wgp;
	uint32_t	sqc_data_cache_size;
	uint32_t	sqc_inst_cache_size;
	uint32_t	gl1c_cache_size;
	uint32_t	gl2c_cache_size;
	uint64_t	mall_size;
	uint32_t	enabled_rb_pipes_mask_hi;
	uint32_t	shadow_size;
	uint32_t	shadow_alignment;
	uint32_t	csa_size;
	uint32_t	csa_alignment;
} r5_amdgpu_info_device;

typedef struct {
	uint32_t	ver;
	uint32_t	feature;
} r5_amdgpu_info_firmware;

typedef struct {
	uint32_t	hw_ip_version_major;
	uint32_t	hw_ip_version_minor;
	uint64_t	capabilities_flags;
	uint32_t	ib_start_alignment;
	uint32_t	ib_size_alignment;
	uint32_t	available_rings;
	uint32_t	ip_discovery_version;
} r5_amdgpu_info_hw_ip;

typedef struct {
	uint32_t			size;
	uint32_t			version;
	r5_amdgpu_info_device		device;
	r5_amdgpu_info_firmware		firmware[R5_AMDGPU_FW_COUNT];
	r5_amdgpu_info_hw_ip		hw_ip[R5_AMDGPU_HW_IP_COUNT];
} r5_gpu_amd_info;

/* external_rev uses the NBIO strap rev, the same source as
 * nbio_v7_0_get_rev_id. Renoir adds 0x91. Barcelo, Cezanne and
 * Green Sardine add 0xa1. The PCI revision is not an input. */
static inline uint32_t
r5_amd_external_rev(uint32_t device, uint32_t strapRev)
{
	if (device == 0x15e7u || device == 0x1638u)
		return strapRev + 0xa1u;
	if (device == 0x1636u || device == 0x164cu)
		return strapRev + 0x91u;
	return strapRev;
}

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	offset;
	uint32_t	value;
} r5_gpu_read_reg;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	context;
} r5_gpu_ctx;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	domain;
	uint32_t	flags;
	uint64_t	bytes;
	uint32_t	handle;
	int32_t		area;
} r5_gpu_bo;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	context;
	uint32_t	handle;
	uint64_t	gpu_va;
	uint64_t	bo_offset;
	uint64_t	map_bytes;
	uint32_t	flags;
	uint32_t	_pad;
} r5_gpu_va;

typedef struct {
	uint64_t	gpu_va;
	uint32_t	dwords;
	uint32_t	flags;
} r5_gpu_ib;

/* A binary syncobj is a timeline. A signal sets the point to the job
 * sequence. A reset sets it to 0. A binary wait waits until the point
 * is greater than 0. R5_GPU_CS_SYNC_BINARY marks that wait or signal. */
#define R5_GPU_CS_SYNC_BINARY 1u

typedef struct {
	uint32_t	handle;
	uint32_t	flags;
	uint64_t	point;
} r5_gpu_cs_sync;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	context;
	uint32_t	ring;
	uint32_t	ib_count;
	uint32_t	wait_count;
	uint32_t	signal_count;
	uint32_t	flags;
	uint64_t	ibs;
	uint64_t	waits;
	uint64_t	signals;
	uint64_t	sequence;
} r5_gpu_cs;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint64_t	sequence;
	uint64_t	timeout_us;
} r5_gpu_fence;

#define R5_GPU_SYNCOBJ_SIGNALED 1u
#define R5_GPU_SYNCOBJ_BINARY 2u

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	handle;
	uint32_t	flags;
	uint64_t	point;
	uint64_t	timeout_us;
} r5_gpu_syncobj;

#endif
