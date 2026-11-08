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
	R5_GPU_BO_IMPORT = R5_GPU_OP_BASE + 18,
	R5_GPU_SYNCOBJ_WAIT_LIST = R5_GPU_OP_BASE + 19,
	R5_GPU_SYNCOBJ_TRANSFER = R5_GPU_OP_BASE + 20,
	R5_GPU_VA_REPLACE = R5_GPU_OP_BASE + 21,
	R5_GPU_VA_CLEAR = R5_GPU_OP_BASE + 22,
	R5_GPU_SELFTEST = R5_GPU_OP_BASE + 23,
	/* One packet type per step, before any other GPU client. Older
	 * callers never send this opcode. */
	R5_GPU_STAGE_P = R5_GPU_OP_BASE + 24,
	/* gpu_debug only. Switches the named GFX9 settings for the self-test. */
	R5_GPU_DEBUG_SWITCH = R5_GPU_OP_BASE + 25,
	/* R1 remap test. Older callers never send this opcode. */
	R5_GPU_STAGE_R1 = R5_GPU_OP_BASE + 26,
	/* Read-only register and setting print for the self-test. */
	R5_GPU_LOG_REGS = R5_GPU_OP_BASE + 27
};

#define R5_GPU_CAP_RENDER 1u
/* Set after the watchdog cancels the device. Older callers ignore it. */
#define R5_GPU_CAP_LOST 2u
/* This file's job caused the loss. Other files keep CAP_LOST only. */
#define R5_GPU_CAP_GUILTY 4u

static inline int
r5_gpu_query_guilty(uint32_t caps)
{
	return (caps & R5_GPU_CAP_LOST) != 0 && (caps & R5_GPU_CAP_GUILTY) != 0;
}

/* Stage P. status is R5_GPU_P_OK, TIMEOUT, PRIV, SKIP or ROOM.
 * first_fail is the 1-based step, or 0 when every step landed.
 * name is the step. ROOM means the packet was not written. */
/* Grown with the extra stage P steps. An older caller fails the
 * size check. The opcode number is unchanged. */
#define R5_GPU_P_STEPS 33u
#define R5_GPU_P_OK 0u
#define R5_GPU_P_TIMEOUT 1u
#define R5_GPU_P_PRIV 2u
#define R5_GPU_P_SKIP 3u
#define R5_GPU_P_ROOM 4u

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	count;
	uint32_t	first_fail;
	uint32_t	status[R5_GPU_P_STEPS];
	char		name[R5_GPU_P_STEPS][24];
} r5_gpu_stage_p;

#define R5_GPU_DOMAIN_VRAM 1u
#define R5_GPU_DOMAIN_GTT 2u

/* AMDGPU_GEM_CREATE_CPU_GTT_USWC, bit 2. The kernel accepts the bit.
 * No RAM area is given a write-combined memory type. A GTT page stays
 * cached and snooped, the same as amdgpu_bo_support_uswc() false. */
#define R5_GPU_BO_WC 4u
/* AMDGPU_GEM_CREATE_VRAM_CLEARED, bit 3. User VRAM is cleared on every
 * allocation, including a span a previous owner freed. */
#define R5_GPU_BO_VRAM_CLEARED 8u

#define R5_GPU_VA_READ 1u
#define R5_GPU_VA_WRITE 2u
#define R5_GPU_VA_EXEC 4u
/* Linux AMDGPU_VM_PAGE_PRT. Handle 0 is valid only with this flag. */
#define R5_GPU_VA_PRT 8u

/* v6.18 amdgpu_vm.h:175
 *   #define AMDGPU_VA_RESERVED_TRAP_SIZE (2ULL << 12)
 * amdgpu_kms.c:968 is high_va_max = AMDGPU_GMC_HOLE_END | vm_size,
 * and vm_size subtracts AMDGPU_VA_RESERVED_TOP (trap 8 KiB + seq64
 * 2 MiB + CSA 2 MiB = 0x402000). That is 0xffffffffffbfe000.
 * v6.12.60 has the same trap define. Mainline
 * 31b8de5e55666f26ea7ece5f412b83eab3f56dbb changes it to
 * (1ULL << 16); stable 4487571ef17a30d274600b3bd6965f497a881299
 * is that change in v6.18.60. This value stays the v6.18 one,
 * which GET_INFO reported on stick 28. */
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
	/* Live buffer bytes. Visible VRAM is the whole BAR on this APU. */
	uint64_t	vram_usage;
	uint64_t	visible_vram_usage;
	uint64_t	gtt_usage;
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

/* A binary syncobj follows the latest submit. Signalled means submitted
 * is greater than 0 and point has caught up. submitted 0 means it was
 * reset or never submitted. A job stores its own sequence, and submitted
 * rises at submit time. point rises at retire. R5_GPU_CS_SYNC_BINARY
 * marks that wait or signal. */
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

/* One wait covers the whole list. ALL waits for every handle. Without it
 * the first finished handle is enough and first_signaled is its index.
 * AVAILABLE waits for the submitted point, not the finished point.
 * BINARY waits until the point is greater than 0. FOR_SUBMIT blocks
 * until the point has been submitted. Without it an unsubmitted point
 * returns B_BAD_VALUE, which is EINVAL, as Linux drm_syncobj_array_wait
 * does. A timeout is only for a point that was submitted. */
#define R5_GPU_WAIT_ALL 1u
#define R5_GPU_WAIT_AVAILABLE 2u
#define R5_GPU_WAIT_BINARY 4u
#define R5_GPU_WAIT_FOR_SUBMIT 8u

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	count;
	uint32_t	flags;
	uint64_t	handles;
	uint64_t	points;
	uint64_t	timeout_us;
	uint32_t	first_signaled;
	uint32_t	_pad;
} r5_gpu_syncobj_wait;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	handle;
	uint32_t	flags;
	uint64_t	point;
	uint64_t	timeout_us;
} r5_gpu_syncobj;

/* dst follows src. src_point 0 is the binary fence: reached when that
 * syncobj is signalled. dst_point 0 attaches dst as binary, with the
 * job sequence. flags must be 0. */
typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	src_handle;
	uint32_t	dst_handle;
	uint64_t	src_point;
	uint64_t	dst_point;
	uint32_t	flags;
	uint32_t	_pad;
} r5_gpu_syncobj_transfer;

/* which is a bit per setting. on is 0 or 1 for every bit that is set.
 * Bit 7 (gfx9_vmid_per_file) stays refused here. The boot setting of
 * that name builds a root per open file. It defaults off.
 * Bit 8 (gfx9_ce_meta) is a boot setting: the CSA is mapped only then. */
#define R5_GPU_SWITCH_INV_RANGE 1u
#define R5_GPU_SWITCH_HUB_LINUX 2u
#define R5_GPU_SWITCH_VMIDS 4u
#define R5_GPU_SWITCH_FLUSH_FENCE 8u
#define R5_GPU_SWITCH_COND_EXEC 16u
#define R5_GPU_SWITCH_GDS 32u
#define R5_GPU_SWITCH_DEFERRED 64u
#define R5_GPU_SWITCH_VMID_PER_FILE 128u
#define R5_GPU_SWITCH_CE_META 256u

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	which;
	uint32_t	on;
} r5_gpu_debug_switch;

/* which is R5_GPU_R1_AB (the writes and the fence) or R5_GPU_R1_C
 * (the indirect-buffer fetch). R1c can stop the command processor, so
 * the self-test runs it after the teapots. */
#define R5_GPU_R1_AB 1u
#define R5_GPU_R1_C 2u

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	which;
	uint32_t	_pad;
	/* This round's pages. y is the next page. A later round uses
	 * another pair, so a leftover translation cannot write a page
	 * this round has freed. */
	uint64_t	x;
	uint64_t	y;
	/* Filled by the kernel from the R1a readback. old_word is the
	 * page that was mapped first. new_word is the page mapped after. */
	uint32_t	old_word;
	uint32_t	new_word;
} r5_gpu_stage_r1;


/* R1a passes when the first page still holds the first write and the
 * page mapped afterwards holds the second write. */
static inline int
r5_gpu_r1_ab_pass(uint32_t old_word, uint32_t new_word)
{
	return old_word == 0x11111111u && new_word == 0x22222222u;
}

/* The kernel fills line and dprints it. The caller writes the file. */
typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	length;
	char		line[480];
} r5_gpu_log_regs;

/* The render node is published only when these three steps are on.
 * Without them the node does not exist, so RADV stays quiet. */
static inline int
r5_gpu_node_on(int kernel, int irq, int gart)
{
	return kernel && irq && gart;
}

/* MC_VM_FB_OFFSET is in 16 MiB units. On an APU that value, shifted,
 * is the system physical address of the carveout. Linux gmc_v9_0_mc_init
 * uses it as aper_base. The low 24 bits of the register are the field. */
static inline uint64_t
r5_gpu_carveout_base(uint32_t fbOffset)
{
	return (uint64_t)(fbOffset & 0x00ffffffu) << 24;
}

#endif
