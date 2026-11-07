/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Device passport. PCI ids for the GFX9 path appear only in this table.
 * Linux v6.12 amdgpu_drv.c lists 15e7, 1636, 1638 and 164c as CHIP_RENOIR.
 * amdgpu_device_parse_gpu_info_fw returns 0 when discovery_bin is set and
 * the chip switch has no Renoir case. linux-firmware ships no
 * renoir_gpu_info.bin. Counts are the IP-discovery record until that
 * binary is parsed. Clocks are the Mesa drm-shim record. The SMU and
 * the UMC table are not read.
 */

#ifndef R5_AMD_DEVICE_H
#define R5_AMD_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
int snprintf(char* str, size_t size, const char* format, ...);
#ifdef __cplusplus
}
#endif

#define R5_AMD_SRC_TABLE \
	"device table; Linux v6.12 amdgpu_drv.c CHIP_RENOIR"
#define R5_AMD_SRC_DISCOVERY \
	"ip discovery not parsed; Linux v6.12 amdgpu_device.c:amdgpu_device_parse_gpu_info_fw"
#define R5_AMD_SRC_REGISTER \
	"register; Linux v6.12 gfx_v9_0.c:gfx_v9_0_get_rb_active_bitmap"
#define R5_AMD_SRC_EARLY \
	"Linux v6.12 gfx_v9_0.c:gfx_v9_0_gpu_early_init IP 9.3.0"
#define R5_AMD_SRC_SW \
	"Linux v6.12 gfx_v9_0.c:gfx_v9_0_sw_init"
#define R5_AMD_SRC_MESA \
	"device table until ATOM firmwareinfo default_sclk and default_mclk; Linux v6.12 amdgpu_atomfirmware.c"
#define R5_AMD_SRC_XCLK \
	"device table until ATOM firmwareinfo SPLL reference (xclk); Linux v6.12 amdgpu_atombios.c"
#define R5_AMD_SRC_FW \
	"common_firmware_header ucode_version byte 16, ucode_feature_version byte 32"
#define R5_AMD_SRC_GPU_INFO \
	"no gpu_info.bin; Linux v6.12 amdgpu_device_parse_gpu_info_fw"
#define R5_AMD_SRC_ATOM \
	"device table until ATOM integratedsysteminfo VRAM type and width; Linux v6.12 amdgpu_atomfirmware.c"
#define R5_AMD_SRC_CU \
	"register; Linux v6.12 gfx_v9_0.c CC_GC_SHADER_ARRAY_CONFIG"

/* gfx_v9_0_get_rb_active_bitmap. CC_RB_BACKEND_DISABLE bits 16..23. */
#define R5_AMD_RB_DISABLE_SHIFT 16u
#define R5_AMD_RB_DISABLE_MASK 0x00ff0000u
/* CC_GC_SHADER_ARRAY_CONFIG inactive CUs in bits 16..31. */
#define R5_AMD_CU_INACTIVE_SHIFT 16u
#define R5_AMD_CU_INACTIVE_MASK 0xffff0000u

typedef struct {
	const char*	name;
	uint32_t	family;
	uint32_t	num_shader_engines;
	uint32_t	num_shader_arrays_per_engine;
	uint32_t	num_cu_per_sh;
	uint32_t	max_backends_per_se;
	uint32_t	num_hw_gfx_contexts;
	uint32_t	ce_ram_size;
	uint32_t	pte_fragment_size;
	uint32_t	max_engine_clock;
	uint32_t	max_memory_clock;
	uint32_t	vram_type;
	uint32_t	vram_bit_width;
	uint32_t	gpu_counter_freq;
	uint32_t	gc_double_offchip_lds_buf;
	uint32_t	num_shader_visible_vgprs;
	uint32_t	num_tcc_blocks;
	uint32_t	gs_vgt_table_depth;
	uint32_t	gs_prim_buffer_depth;
	uint32_t	max_gs_waves_per_vgt;
	uint32_t	hw_major;
	uint32_t	hw_minor;
	uint32_t	ids_flags;
	uint32_t	ib_start_alignment;
	uint32_t	ib_size_alignment;
	uint32_t	wave_front_size;
	uint32_t	page_size;
} r5_amd_gen;

typedef struct {
	uint32_t	pci_id;
	uint32_t	rev_add;
	uint32_t	gfx9_cp;
	uint32_t	need_reset;
	uint32_t	lucienne;
	const char*	fw_stem;
	const r5_amd_gen* gen;
} r5_amd_row;

/* One generation row. The four Renoir PCI ids share it. */
static const r5_amd_gen kR5AmdGc93 = {
	"GC 9.3 Renoir", 142u,
	1u, 1u, 8u, 2u, 8u, 0x8000u, 2097152u,
	1800000u, 1333000u, 11u, 128u, 100000u, 1u,
	256u, 4u, 32u, 1792u, 32u, 9u, 3u, 1u, 32u, 4u, 64u, 4096u
};

/* rev_add: nbio strap plus the external-rev offset. Green Sardine and
 * Cezanne add 0xa1. Renoir and Lucienne add 0x91. */
static const r5_amd_row kR5AmdRows[] = {
	{0x15e7u, 0xa1u, 1u, 1u, 0u, "green_sardine", &kR5AmdGc93},
	{0x1638u, 0xa1u, 1u, 1u, 0u, "green_sardine", &kR5AmdGc93},
	{0x1636u, 0x91u, 1u, 1u, 0u, "renoir", &kR5AmdGc93},
	{0x164cu, 0x91u, 0u, 1u, 1u, "renoir", &kR5AmdGc93},
};

typedef struct {
	uint32_t	device_id;
	uint32_t	chip_rev;
	uint32_t	external_rev;
	uint32_t	pci_rev;
	uint32_t	family;
	uint32_t	num_shader_engines;
	uint32_t	num_shader_arrays_per_engine;
	uint32_t	cu_active_number;
	uint32_t	cu_bitmap;
	uint32_t	num_cu_per_sh;
	uint32_t	num_rb_pipes;
	uint32_t	enabled_rb_pipes_mask;
	uint32_t	num_hw_gfx_contexts;
	uint32_t	ce_ram_size;
	uint32_t	pte_fragment_size;
	uint32_t	max_engine_clock;
	uint32_t	min_engine_clock;
	uint32_t	max_memory_clock;
	uint32_t	min_memory_clock;
	uint32_t	vram_type;
	uint32_t	vram_bit_width;
	uint32_t	gpu_counter_freq;
	uint32_t	gc_double_offchip_lds_buf;
	uint32_t	num_shader_visible_vgprs;
	uint32_t	num_tcc_blocks;
	uint32_t	gs_vgt_table_depth;
	uint32_t	gs_prim_buffer_depth;
	uint32_t	max_gs_waves_per_vgt;
	uint32_t	ids_flags;
	uint32_t	hw_major;
	uint32_t	hw_minor;
	uint32_t	ib_start_alignment;
	uint32_t	ib_size_alignment;
	uint32_t	wave_front_size;
	uint32_t	page_size;
	uint32_t	gpu_info_loaded;
	uint32_t	atom_read;
	uint32_t	fw_ver[32];
	uint32_t	fw_feature[32];
	uint32_t	known;
	uint32_t	discovery_loaded;
} r5_amd_device;

/* Linux v6.12 amdgpu_discovery.h. The binary sits DISCOVERY_TMR_OFFSET
 * bytes below the end of VRAM and is DISCOVERY_TMR_SIZE bytes. */
#define R5_DISCOVERY_SIGNATURE 0x28211407u
#define R5_DISCOVERY_TMR_SIZE (10u << 10)
#define R5_DISCOVERY_TMR_OFFSET (64u << 10)

static inline const r5_amd_row*
r5_amd_row_find(uint32_t pci)
{
	for (uint32_t i = 0; i < sizeof(kR5AmdRows) / sizeof(kR5AmdRows[0]); i++) {
		if (kR5AmdRows[i].pci_id == pci)
			return &kR5AmdRows[i];
	}
	return NULL;
}

static inline uint32_t
r5_amd_rev_add(uint32_t pci)
{
	const r5_amd_row* row = r5_amd_row_find(pci);
	if (row == NULL)
		return 0;
	return row->rev_add;
}

static inline const char*
r5_amd_fw_stem(uint32_t pci)
{
	const r5_amd_row* row = r5_amd_row_find(pci);
	if (row == NULL)
		return NULL;
	return row->fw_stem;
}

static inline int
r5_amd_gfx9_cp(uint32_t pci)
{
	const r5_amd_row* row = r5_amd_row_find(pci);
	return row != NULL && row->gfx9_cp != 0;
}

static inline int
r5_amd_lucienne(uint32_t pci)
{
	const r5_amd_row* row = r5_amd_row_find(pci);
	return row != NULL && row->lucienne != 0;
}

static inline int
r5_amd_need_reset(uint32_t pci)
{
	const r5_amd_row* row = r5_amd_row_find(pci);
	return row != NULL && row->need_reset != 0;
}

static inline uint32_t
r5_amd_cu_bitmap(uint32_t cc, uint32_t user, uint32_t cuPerSh, uint32_t* count)
{
	uint32_t inactive = ((cc | user) & R5_AMD_CU_INACTIVE_MASK)
		>> R5_AMD_CU_INACTIVE_SHIFT;
	uint32_t width = cuPerSh > 31u ? 31u : cuPerSh;
	uint32_t mask = width == 0 ? 0 : ((1u << width) - 1u);
	uint32_t bitmap = (~inactive) & mask;
	uint32_t n = 0;
	for (uint32_t bit = bitmap; bit != 0; bit >>= 1)
		n += bit & 1u;
	if (count != NULL)
		*count = n;
	return bitmap;
}

static inline uint32_t
r5_amd_rb_mask(uint32_t cc, uint32_t user, uint32_t backends)
{
	uint32_t disabled = ((cc | user) & R5_AMD_RB_DISABLE_MASK)
		>> R5_AMD_RB_DISABLE_SHIFT;
	uint32_t width = backends > 31u ? 31u : backends;
	uint32_t bits = width == 0 ? 0 : ((1u << width) - 1u);
	return (~disabled) & bits;
}

/* Order: device table, then the discovery note (no blob), then the
 * register masks, then firmware headers. ATOM is not applied. */
static inline void
r5_amd_device_fill(r5_amd_device* dev, uint32_t pci, uint32_t strap,
	uint32_t pciRev, uint32_t rbCc, uint32_t rbUser, uint32_t cuCc,
	uint32_t cuUser, const uint32_t* fwVer, const uint32_t* fwFeature)
{
	const r5_amd_row* row;
	uint32_t count = 0;
	memset(dev, 0, sizeof(*dev));
	dev->device_id = pci;
	dev->chip_rev = strap;
	dev->pci_rev = pciRev;
	dev->external_rev = strap + r5_amd_rev_add(pci);
	dev->gpu_info_loaded = 0;
	dev->atom_read = 0;
	row = r5_amd_row_find(pci);
	if (row == NULL)
		return;
	dev->known = 1;
	dev->family = row->gen->family;
	dev->num_shader_engines = row->gen->num_shader_engines;
	dev->num_shader_arrays_per_engine = row->gen->num_shader_arrays_per_engine;
	dev->num_cu_per_sh = row->gen->num_cu_per_sh;
	dev->num_rb_pipes = row->gen->max_backends_per_se * row->gen->num_shader_engines;
	dev->num_hw_gfx_contexts = row->gen->num_hw_gfx_contexts;
	dev->ce_ram_size = row->gen->ce_ram_size;
	dev->pte_fragment_size = row->gen->pte_fragment_size;
	dev->max_engine_clock = row->gen->max_engine_clock;
	dev->min_engine_clock = row->gen->max_engine_clock;
	dev->max_memory_clock = row->gen->max_memory_clock;
	dev->min_memory_clock = row->gen->max_memory_clock;
	dev->vram_type = row->gen->vram_type;
	dev->vram_bit_width = row->gen->vram_bit_width;
	dev->gpu_counter_freq = row->gen->gpu_counter_freq;
	dev->gc_double_offchip_lds_buf = row->gen->gc_double_offchip_lds_buf;
	dev->num_shader_visible_vgprs = row->gen->num_shader_visible_vgprs;
	dev->num_tcc_blocks = row->gen->num_tcc_blocks;
	dev->gs_vgt_table_depth = row->gen->gs_vgt_table_depth;
	dev->gs_prim_buffer_depth = row->gen->gs_prim_buffer_depth;
	dev->max_gs_waves_per_vgt = row->gen->max_gs_waves_per_vgt;
	dev->ids_flags = row->gen->ids_flags;
	dev->hw_major = row->gen->hw_major;
	dev->hw_minor = row->gen->hw_minor;
	dev->ib_start_alignment = row->gen->ib_start_alignment;
	dev->ib_size_alignment = row->gen->ib_size_alignment;
	dev->wave_front_size = row->gen->wave_front_size;
	dev->page_size = row->gen->page_size;
	dev->cu_bitmap = r5_amd_cu_bitmap(cuCc, cuUser, row->gen->num_cu_per_sh, &count);
	dev->cu_active_number = count;
	dev->enabled_rb_pipes_mask = r5_amd_rb_mask(rbCc, rbUser,
		row->gen->max_backends_per_se);
	if (fwVer != NULL && fwFeature != NULL) {
		for (uint32_t i = 0; i < 32; i++) {
			dev->fw_ver[i] = fwVer[i];
			dev->fw_feature[i] = fwFeature[i];
		}
	}
}

static inline uint16_t
r5_le16(const uint8_t* p)
{
	uint16_t v;
	memcpy(&v, p, 2);
	return v;
}


static inline uint32_t
r5_le32(const uint8_t* p)
{
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}


/* Linux common_firmware_header. The device table does not carry these. */
static inline int
r5_amd_ucode_versions(const uint8_t* blob, uint32_t bytes,
	uint32_t* version, uint32_t* feature)
{
	uint32_t ver;
	uint32_t feat;
	if (blob == NULL || bytes < 36)
		return 0;
	ver = r5_le32(blob + 16);
	feat = r5_le32(blob + 32);
	if (version != NULL)
		*version = ver;
	if (feature != NULL)
		*feature = feat;
	return 1;
}


static inline uint16_t
r5_discovery_sum(const uint8_t* p, uint32_t n)
{
	uint32_t sum = 0;
	for (uint32_t i = 0; i < n; i++)
		sum += p[i];
	return (uint16_t)sum;
}


/* amdgpu_discovery_init checks the signature and the checksum of the
 * bytes after binary_checksum. amdgpu_discovery_get_gfx_info then reads
 * GC table index 1. A valid table replaces the device-table counts.
 * The device table is the mismatch cross-check. Returns 1 when the
 * GC table was applied. */
static inline int
r5_amd_discovery_apply(r5_amd_device* dev, const void* binary, uint32_t bytes,
	char* mismatch, int mismatchCap)
{
	const uint8_t* bin = (const uint8_t*)binary;
	uint32_t se;
	uint32_t cu;
	uint32_t sh;
	uint32_t rb;
	uint32_t tcc;
	uint32_t gprs;
	uint32_t gsTable;
	uint32_t gsPrim;
	uint32_t offchip;
	uint16_t gcOff;
	uint16_t gcSize;
	uint16_t gcSum;
	uint16_t major;
	const uint8_t* gc;
	const r5_amd_row* row;
	if (mismatch != NULL && mismatchCap > 0)
		mismatch[0] = 0;
	if (bin == NULL || bytes < 60)
		return 0;
	if (r5_le32(bin) != R5_DISCOVERY_SIGNATURE)
		return 0;
	{
		uint16_t stored = r5_le16(bin + 8);
		uint16_t size = r5_le16(bin + 10);
		if (size < 12 || size > bytes)
			return 0;
		if (r5_discovery_sum(bin + 10, (uint32_t)size - 10u) != stored)
			return 0;
	}
	gcOff = r5_le16(bin + 12 + 8);
	gcSum = r5_le16(bin + 12 + 8 + 2);
	gcSize = r5_le16(bin + 12 + 8 + 4);
	if (gcOff == 0 || gcSize < 12 + 11u * 4u || (uint32_t)gcOff + gcSize > bytes)
		return 0;
	if (r5_discovery_sum(bin + gcOff, gcSize) != gcSum)
		return 0;
	gc = bin + gcOff;
	major = r5_le16(gc + 4);
	if (major == 1) {
		uint32_t wgp0 = r5_le32(gc + 12 + 4);
		uint32_t wgp1 = r5_le32(gc + 12 + 8);
		se = r5_le32(gc + 12);
		cu = 2u * (wgp0 + wgp1);
		sh = r5_le32(gc + 12 + 16u * 4u);
		rb = r5_le32(gc + 12 + 12);
		tcc = r5_le32(gc + 12 + 16);
		gprs = r5_le32(gc + 12 + 20);
		gsTable = r5_le32(gc + 12 + 28);
		gsPrim = r5_le32(gc + 12 + 32);
		offchip = r5_le32(gc + 12 + 40);
	} else if (major == 2) {
		se = r5_le32(gc + 12);
		cu = r5_le32(gc + 12 + 4);
		sh = r5_le32(gc + 12 + 8);
		rb = r5_le32(gc + 12 + 12);
		tcc = r5_le32(gc + 12 + 16);
		gprs = r5_le32(gc + 12 + 20);
		gsTable = r5_le32(gc + 12 + 28);
		gsPrim = r5_le32(gc + 12 + 32);
		offchip = r5_le32(gc + 12 + 40);
	} else
		return 0;
	if (dev == NULL)
		return 1;
	row = r5_amd_row_find(dev->device_id);
	if (row != NULL && mismatch != NULL && mismatchCap > 1
		&& (se != row->gen->num_shader_engines
			|| sh != row->gen->num_shader_arrays_per_engine
			|| cu != row->gen->num_cu_per_sh || rb != row->gen->max_backends_per_se
			|| gprs != row->gen->num_shader_visible_vgprs
			|| gsTable != row->gen->gs_vgt_table_depth
			|| gsPrim != row->gen->gs_prim_buffer_depth
			|| offchip != row->gen->gc_double_offchip_lds_buf
			|| tcc != row->gen->num_tcc_blocks)) {
		snprintf(mismatch, (size_t)mismatchCap,
			"discovery mismatch se %u/%u cu %u/%u",
			se, row->gen->num_shader_engines, cu, row->gen->num_cu_per_sh);
	}
	dev->num_shader_engines = se;
	dev->num_shader_arrays_per_engine = sh;
	dev->num_cu_per_sh = cu;
	dev->num_rb_pipes = rb * se;
	dev->num_tcc_blocks = tcc;
	dev->num_shader_visible_vgprs = gprs;
	dev->gs_vgt_table_depth = gsTable;
	dev->gs_prim_buffer_depth = gsPrim;
	dev->gc_double_offchip_lds_buf = offchip;
	dev->discovery_loaded = 1;
	dev->gpu_info_loaded = 1;
	return 1;
}


static inline int
r5_amd_passport_format(const r5_amd_device* dev, char* buf, int cap)
{
	int n = 0;
	int wrote;
	const char* disc;
	if (buf == NULL || cap < 1)
		return 0;
	buf[0] = 0;
	disc = dev->discovery_loaded
		? "ip discovery GC table; Linux v6.12 amdgpu_discovery_get_gfx_info"
		: R5_AMD_SRC_DISCOVERY;
#define R5_LINE(fmt, ...) \
	do { \
		if (n >= cap - 1) \
			break; \
		wrote = snprintf(buf + n, (size_t)(cap - n), fmt, __VA_ARGS__); \
		if (wrote < 0) \
			break; \
		n += wrote; \
	} while (0)
	R5_LINE("device_id %x (%s)\n", dev->device_id, R5_AMD_SRC_TABLE);
	R5_LINE("chip_rev %u (%s)\n", dev->chip_rev, R5_AMD_SRC_REGISTER);
	R5_LINE("external_rev %x (%s)\n", dev->external_rev, R5_AMD_SRC_TABLE);
	R5_LINE("family %u (%s)\n", dev->family, R5_AMD_SRC_TABLE);
	R5_LINE("num_shader_engines %u (%s)\n", dev->num_shader_engines, disc);
	R5_LINE("num_shader_arrays_per_engine %u (%s)\n",
		dev->num_shader_arrays_per_engine, disc);
	R5_LINE("num_cu_per_sh %u (%s)\n", dev->num_cu_per_sh, disc);
	R5_LINE("cu_active_number %u (%s)\n", dev->cu_active_number, R5_AMD_SRC_CU);
	R5_LINE("cu_bitmap %x (%s)\n", dev->cu_bitmap, R5_AMD_SRC_CU);
	R5_LINE("num_rb_pipes %u (%s)\n", dev->num_rb_pipes, disc);
	R5_LINE("enabled_rb_pipes_mask %x (%s)\n", dev->enabled_rb_pipes_mask,
		R5_AMD_SRC_REGISTER);
	R5_LINE("num_hw_gfx_contexts %u (%s)\n", dev->num_hw_gfx_contexts,
		R5_AMD_SRC_EARLY);
	R5_LINE("ce_ram_size %x (%s)\n", dev->ce_ram_size, R5_AMD_SRC_SW);
	R5_LINE("pte_fragment_size %u (%s)\n", dev->pte_fragment_size,
		R5_AMD_SRC_EARLY);
	R5_LINE("max_engine_clock %u (%s)\n", dev->max_engine_clock, R5_AMD_SRC_MESA);
	R5_LINE("min_engine_clock %u (%s)\n", dev->min_engine_clock, R5_AMD_SRC_MESA);
	R5_LINE("max_memory_clock %u (%s)\n", dev->max_memory_clock, R5_AMD_SRC_MESA);
	R5_LINE("min_memory_clock %u (%s)\n", dev->min_memory_clock, R5_AMD_SRC_MESA);
	R5_LINE("vram_type %u (%s)\n", dev->vram_type, R5_AMD_SRC_MESA);
	R5_LINE("vram_bit_width %u (%s)\n", dev->vram_bit_width, R5_AMD_SRC_MESA);
	R5_LINE("gpu_counter_freq %u (%s)\n", dev->gpu_counter_freq, R5_AMD_SRC_XCLK);
	R5_LINE("gc_double_offchip_lds_buf %u (%s)\n",
		dev->gc_double_offchip_lds_buf,
		dev->discovery_loaded ? disc : R5_AMD_SRC_EARLY);
	R5_LINE("num_shader_visible_vgprs %u (%s)\n",
		dev->num_shader_visible_vgprs,
		dev->discovery_loaded ? disc : R5_AMD_SRC_EARLY);
	R5_LINE("num_tcc_blocks %u (%s)\n", dev->num_tcc_blocks,
		dev->discovery_loaded ? disc : R5_AMD_SRC_EARLY);
	R5_LINE("gs_vgt_table_depth %u (%s)\n", dev->gs_vgt_table_depth,
		dev->discovery_loaded ? disc : R5_AMD_SRC_EARLY);
	R5_LINE("gs_prim_buffer_depth %u (%s)\n", dev->gs_prim_buffer_depth,
		dev->discovery_loaded ? disc : R5_AMD_SRC_EARLY);
	R5_LINE("max_gs_waves_per_vgt %u (%s)\n", dev->max_gs_waves_per_vgt,
		R5_AMD_SRC_EARLY);
	R5_LINE("hw_ip %u.%u (%s)\n", dev->hw_major, dev->hw_minor, R5_AMD_SRC_EARLY);
	R5_LINE("gpu_info_loaded %u (%s)\n", dev->gpu_info_loaded, R5_AMD_SRC_GPU_INFO);
	R5_LINE("atom_read %u (%s)\n", dev->atom_read, R5_AMD_SRC_ATOM);
	R5_LINE("firmware_me %u feature %u (%s)\n", dev->fw_ver[4],
		dev->fw_feature[4], R5_AMD_SRC_FW);
#undef R5_LINE
	if (n >= cap)
		buf[cap - 1] = 0;
	return n;
}

#endif
