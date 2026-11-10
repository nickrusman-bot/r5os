/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The device passport. One GC 9.3 row is the cross-check. Discovery
 * and the ATOM tables are the values that are reported. Field offsets
 * are the Linux 6.12.60 layout, cited below. This file does not copy
 * that parser. The ring, the firmware blobs, and the K path are not
 * read here.
 */

#include "r5_passport.h"
#include "radeon_hd_private.h"

#include <KernelExport.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>


/* Linux v6.12.60 drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.h */
static const uint32 kDiscoveryBytes = 10u << 10;
static const uint64 kDiscoveryOffset = 64u << 10;
/* include/discovery.h BINARY_SIGNATURE, GC table index 1, table id. */
static const uint32 kDiscoverySignature = 0x28211407u;
static const uint32 kGcTableId = 0x4347u;
static const uint32 kGcTable = 1;

/* atomfirmware.h atom_master_list_of_data_tables_v2_1. */
static const uint32 kAtomFirmwareInfo = 4;
static const uint32 kAtomSmuInfo = 8;
static const uint32 kAtomIntegrated = 30;

/* GC 9.3 cross-check. Discovery replaces a field when it disagrees. */
static const uint32 kTableSe = 1;
static const uint32 kTableSh = 1;
static const uint32 kTableCu = 8;
static const uint32 kTableRb = 2;
static const uint32 kTableVgpr = 256;
static const uint32 kTableTcc = 4;
static const uint32 kTableGsWaves = 32;
static const uint32 kTableGsDepth = 32;
static const uint32 kTableGsPrim = 1792;
static const uint32 kTableOffchip = 1;
static const uint32 kTableWave = 64;


static bool
read_u16(const uint8* base, uint32 size, uint32 offset, uint16* out)
{
	if (base == NULL || out == NULL || offset + 2u > size)
		return false;
	memcpy(out, base + offset, 2);
	return true;
}


static bool
read_u32(const uint8* base, uint32 size, uint32 offset, uint32* out)
{
	if (base == NULL || out == NULL || offset + 4u > size)
		return false;
	memcpy(out, base + offset, 4);
	return true;
}


static uint16
sum_bytes(const uint8* base, uint32 size)
{
	uint32 sum = 0;
	for (uint32 i = 0; i < size; i++)
		sum += base[i];
	return (uint16)sum;
}


/* The PCI ids live in the radeon_hd device table in driver.cpp.
 * 15e7, 1636, 1638 and 164c (Lucienne) are RADEON_RENOIR there. */
static bool
known_gc93(radeon_info* info)
{
	return info != NULL && info->chipsetID == RADEON_RENOIR;
}


static bool sSaid = false;


static void
say(const char* format, ...)
{
	char line[320];
	va_list args;
	if (sSaid)
		return;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	dprintf("%s", line);
}


static void
mismatch(const char* field, uint32 table, uint32 discovery)
{
	if (table == discovery)
		return;
	say("radeon_hd: passport mismatch %s table %lu discovery %lu\n",
		field, (unsigned long)table, (unsigned long)discovery);
}


static void
apply_row(r5_gpu_amd_info* amd, uint32* cuPerSh, uint32* rbPerSe)
{
	amd->device.family = R5_AMDGPU_FAMILY_RV;
	amd->device.ids_flags = R5_AMDGPU_IDS_FLAGS_FUSION;
	amd->device.num_shader_engines = kTableSe;
	amd->device.num_shader_arrays_per_engine = kTableSh;
	amd->device.num_cu_per_sh = kTableCu;
	/* Linux v6.12 gfx_v9_0.c gfx_v9_0_gpu_early_init, IP 9.3.0. */
	amd->device.num_hw_gfx_contexts = 8;
	/* Linux v6.12 gfx_v9_0.c gfx_v9_0_sw_init. The same for every GFX9. */
	amd->device.ce_ram_size = 0x8000;
	amd->device.gc_double_offchip_lds_buf = kTableOffchip;
	amd->device.num_shader_visible_vgprs = kTableVgpr;
	amd->device.num_tcc_blocks = kTableTcc;
	amd->device.gs_vgt_table_depth = kTableGsDepth;
	amd->device.gs_prim_buffer_depth = kTableGsPrim;
	amd->device.max_gs_waves_per_vgt = kTableGsWaves;
	amd->device.wave_front_size = kTableWave;
	amd->hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_major = 9;
	amd->hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_minor = 3;
	if (cuPerSh != NULL)
		*cuPerSh = kTableCu;
	if (rbPerSe != NULL)
		*rbPerSe = kTableRb;
}


static bool
discovery_gc(radeon_info* info, uint32* se, uint32* sh, uint32* cu,
	uint32* rb, uint32* gl2c, uint32* gprs, uint32* gsDepth,
	uint32* gsPrim, uint32* offchip, uint32* wave)
{
	if (info == NULL || info->carveout == NULL
		|| info->carveout_bytes < kDiscoveryOffset) {
		say("radeon_hd: passport discovery missing\n");
		return false;
	}
	const uint8* bin = info->carveout
		+ (info->carveout_bytes - kDiscoveryOffset);
	uint32 signature = 0;
	uint16 stored = 0;
	uint16 binarySize = 0;
	if (!read_u32(bin, kDiscoveryBytes, 0, &signature)
		|| signature != kDiscoverySignature
		|| !read_u16(bin, kDiscoveryBytes, 8, &stored)
		|| !read_u16(bin, kDiscoveryBytes, 10, &binarySize)
		|| binarySize <= 10 || binarySize > kDiscoveryBytes) {
		say("radeon_hd: passport discovery missing\n");
		return false;
	}
	/* Bytes after binary_checksum. Linux amdgpu_discovery_init. */
	if (sum_bytes(bin + 10, (uint32)binarySize - 10u) != stored) {
		say("radeon_hd: passport discovery missing\n");
		return false;
	}
	uint16 gcOff = 0;
	uint16 gcSum = 0;
	uint16 gcSize = 0;
	uint32 infoAt = 12u + kGcTable * 8u;
	if (!read_u16(bin, binarySize, infoAt, &gcOff)
		|| !read_u16(bin, binarySize, infoAt + 2u, &gcSum)
		|| !read_u16(bin, binarySize, infoAt + 4u, &gcSize)
		|| gcOff == 0 || gcSize < 12u + 11u * 4u
		|| (uint32)gcOff + gcSize > binarySize
		|| sum_bytes(bin + gcOff, gcSize) != gcSum) {
		say("radeon_hd: passport discovery missing\n");
		return false;
	}
	uint32 tableId = 0;
	uint16 major = 0;
	if (!read_u32(bin, binarySize, gcOff, &tableId) || tableId != kGcTableId
		|| !read_u16(bin, binarySize, gcOff + 4u, &major) || major != 1) {
		say("radeon_hd: passport discovery missing\n");
		return false;
	}
	/* gc_info_v1_0, after the 12-byte gpu_info_header.
	 * amdgpu_discovery_get_gfx_info version 1. */
	uint32 base = gcOff + 12u;
	uint32 wgp0 = 0;
	uint32 wgp1 = 0;
	if (!read_u32(bin, binarySize, base, se)
		|| !read_u32(bin, binarySize, base + 4u, &wgp0)
		|| !read_u32(bin, binarySize, base + 8u, &wgp1)
		|| !read_u32(bin, binarySize, base + 12u, rb)
		|| !read_u32(bin, binarySize, base + 16u, gl2c)
		|| !read_u32(bin, binarySize, base + 20u, gprs)
		|| !read_u32(bin, binarySize, base + 28u, gsDepth)
		|| !read_u32(bin, binarySize, base + 32u, gsPrim)
		|| !read_u32(bin, binarySize, base + 40u, offchip)
		|| !read_u32(bin, binarySize, base + 44u, wave)
		|| !read_u32(bin, binarySize, base + 64u, sh)) {
		say("radeon_hd: passport discovery missing\n");
		return false;
	}
	*cu = 2u * (wgp0 + wgp1);
	return true;
}


static bool
atom_size(const uint8* rom, uint32* size)
{
	uint16 magic = 0;
	uint16 units = 0;
	if (!read_u16(rom, 2, 0, &magic) || magic != 0xaa55u)
		return false;
	if (!read_u16(rom, 4, 2, &units) || units == 0)
		return false;
	uint32 bytes = (uint32)units * 512u;
	if (bytes < 0x60 || bytes > 512u * 1024u)
		return false;
	*size = bytes;
	return true;
}


static bool
atom_table(const uint8* rom, uint32 size, uint32 index, uint16* offset,
	uint8* format, uint8* content, uint16* tableSize)
{
	uint16 romOff = 0;
	if (!read_u16(rom, size, 0x48, &romOff) || romOff + 0x24u > size)
		return false;
	if (memcmp(rom + romOff + 4, "ATOM", 4) != 0
		&& memcmp(rom + romOff + 4, "MOTA", 4) != 0)
		return false;
	uint16 dataOff = 0;
	if (!read_u16(rom, size, romOff + 0x20, &dataOff) || dataOff + 6u > size)
		return false;
	/* Master data table v2.1. format_revision is byte 2. */
	if (rom[dataOff + 2] < 2)
		return false;
	uint16 entry = 0;
	if (!read_u16(rom, size, dataOff + 4u + index * 2u, &entry)
		|| entry == 0 || entry + 4u > size)
		return false;
	uint16 bytes = 0;
	if (!read_u16(rom, size, entry, &bytes) || bytes < 4u
		|| entry + bytes > size)
		return false;
	*offset = entry;
	*format = rom[entry + 2];
	*content = rom[entry + 3];
	*tableSize = bytes;
	return true;
}


static void
apply_atom(radeon_info* info, r5_gpu_amd_info* amd)
{
	uint32 size = 0;
	if (info == NULL || info->atom_buffer == NULL
		|| !atom_size(info->atom_buffer, &size)) {
		say("radeon_hd: passport atom missing\n");
		return;
	}
	const uint8* rom = info->atom_buffer;
	uint16 offset = 0;
	uint8 format = 0;
	uint8 content = 0;
	uint16 tableSize = 0;
	if (atom_table(rom, size, kAtomFirmwareInfo, &offset, &format, &content,
			&tableSize) && format == 3 && tableSize >= 16) {
		uint32 sclk = 0;
		uint32 mclk = 0;
		/* atom_firmware_info_v3_1 bootup clocks, in 10 kHz. */
		if (read_u32(rom, size, offset + 8u, &sclk)
			&& read_u32(rom, size, offset + 12u, &mclk)) {
			amd->device.max_engine_clock = sclk * 10u;
			amd->device.min_engine_clock = sclk * 10u;
			amd->device.max_memory_clock = mclk * 10u;
			amd->device.min_memory_clock = mclk * 10u;
		}
	}
	if (atom_table(rom, size, kAtomSmuInfo, &offset, &format, &content,
			&tableSize) && (format == 3 || format == 4)
			&& tableSize >= 20) {
		uint32 ref = 0;
		/* v3.1 and v4.0 both store core_refclk_10khz at byte 16. */
		if (read_u32(rom, size, offset + 16u, &ref))
			amd->device.gpu_counter_freq = ref * 10u;
	}
	if (!atom_table(rom, size, kAtomIntegrated, &offset, &format, &content,
			&tableSize)) {
		say("radeon_hd: passport atom missing\n");
		return;
	}
	uint32 typeAt = 0;
	if (format == 1 && (content == 11 || content == 12) && tableSize >= 44)
		typeAt = offset + 42u;
	else if (format == 2 && content >= 1 && content <= 3 && tableSize >= 28)
		typeAt = offset + 26u;
	else {
		say("radeon_hd: passport atom missing\n");
		return;
	}
	uint8 memoryType = rom[typeAt];
	uint8 channels = rom[typeAt + 1];
	if (channels == 0)
		channels = 1;
	uint32 kind = 0;
	/* Linux v6.12 amdgpu_atomfirmware.c
	 * convert_atom_mem_type_to_vram_type. 19 and 28 are Ddr2 (2),
	 * 24 and 29 are Ddr3 (7), 26 is Ddr4 (8), 30 is LpDdr4 (11),
	 * 35 is LpDdr5 (12). The width rule in that file is 32 bits per
	 * channel for LpDdr5 and 64 for the others. */
	if (memoryType == 19 || memoryType == 28)
		kind = 2;
	else if (memoryType == 24 || memoryType == 29)
		kind = 7;
	else if (memoryType == 26)
		kind = 8;
	else if (memoryType == 30)
		kind = 11;
	else if (memoryType == 35)
		kind = 12;
	amd->device.vram_type = kind;
	amd->device.vram_bit_width = (uint32)channels * (kind == 12 ? 32u : 64u);
}


void
r5_passport_apply(radeon_info* info, r5_gpu_amd_info* amd, uint32_t* cuPerSh,
	uint32_t* rbPerSe)
{
	if (amd == NULL || info == NULL || info->pci == NULL)
		return;
	bool row = known_gc93(info);
	if (row)
		apply_row(amd, cuPerSh, rbPerSe);
	else
		say("radeon_hd: passport no row\n");

	uint32 se = 0;
	uint32 sh = 0;
	uint32 cu = 0;
	uint32 rb = 0;
	uint32 gl2c = 0;
	uint32 gprs = 0;
	uint32 gsDepth = 0;
	uint32 gsPrim = 0;
	uint32 offchip = 0;
	uint32 wave = 0;
	if (discovery_gc(info, &se, &sh, &cu, &rb, &gl2c, &gprs, &gsDepth,
			&gsPrim, &offchip, &wave)) {
		if (row) {
			mismatch("se", kTableSe, se);
			mismatch("sh", kTableSh, sh);
			mismatch("cu", kTableCu, cu);
			mismatch("rb", kTableRb, rb);
			mismatch("vgpr", kTableVgpr, gprs);
			mismatch("tcc", kTableTcc, gl2c);
			mismatch("gs_depth", kTableGsDepth, gsDepth);
			mismatch("gs_prim", kTableGsPrim, gsPrim);
			mismatch("offchip", kTableOffchip, offchip);
			mismatch("wave", kTableWave, wave);
		}
		amd->device.num_shader_engines = se;
		amd->device.num_shader_arrays_per_engine = sh;
		amd->device.num_cu_per_sh = cu;
		amd->device.num_shader_visible_vgprs = gprs;
		amd->device.num_tcc_blocks = gl2c;
		amd->device.gs_vgt_table_depth = gsDepth;
		amd->device.gs_prim_buffer_depth = gsPrim;
		amd->device.gc_double_offchip_lds_buf = offchip;
		amd->device.wave_front_size = wave;
		if (cuPerSh != NULL && cu > 0 && cu <= 16)
			*cuPerSh = cu;
		if (rbPerSe != NULL && rb > 0 && rb <= 8)
			*rbPerSe = rb;
		say("radeon_hd: passport discovery se %lu sh %lu cu %lu\n",
			(unsigned long)se, (unsigned long)sh, (unsigned long)cu);
	}
	apply_atom(info, amd);
	{
		uint32 atomFreq = amd->device.gpu_counter_freq;
		/* Linux tag v6.12, drivers/gpu/drm/amd/amdgpu/soc15.c
		 * soc15_get_xclk() lines 326-330: MP1 12.0.0 and 12.0.1
		 * return 10000. The same return covers 13.0.6 and 13.0.14.
		 * amdgpu_kms.c line 863 stores amdgpu_asic_get_xclk() * 10.
		 * Renoir is MP1 12.0. The ATOM clock is the cross-check. */
		if (known_gc93(info))
			amd->device.gpu_counter_freq = 100000;
		say("radeon_hd: passport counter_freq %lu atom %lu\n",
			(unsigned long)amd->device.gpu_counter_freq,
			(unsigned long)atomFreq);
	}
	/* Linux tag v6.12 amdgpu_discovery.c
	 * amdgpu_discovery_set_ip_blocks() starts at line 2425 and has
	 * no CHIP_RENOIR case. The Raven case at line 2474 sets IP
	 * versions by hand. passport discovery missing stays the line
	 * this chip prints when the binary is absent. */
	sSaid = true;
}
