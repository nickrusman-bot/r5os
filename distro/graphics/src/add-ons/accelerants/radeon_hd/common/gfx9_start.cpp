/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GFX9 start for GC 9.3. The packet bytes are the sequence that landed the
 * fence, the indirect buffer, the DMA, and the compute dispatch. This file
 * is the only copy. The accelerant and the kernel driver both link it.
 * The user chose GPL-3.0-or-later for this file on 5 Nov 2026.
 */

#include "gfx9_start.h"
#include "r5_agp.h"

#include "clearstate_defs.h"
#include "clearstate_gfx9.h"
#include "gfx9_compute_ib.h"
#include "gfx9_compute_shader.h"
#include "radeon_hd.h"

#include <OS.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static r5_gfx9_host* sHost = NULL;
static uint32 sPciId = 0;
static uint64 sVramBytes = 0;
static r5_gfx9_results* sResults = NULL;
static bool sK0Ok = false;
static int sHubAperture = 0;
static int sAperturePlus = 0;
static r5_gfx9_hub_fn sHubProgram = NULL;


void
r5_gfx9_set_hub_aperture(int on)
{
	sHubAperture = on ? 1 : 0;
}


void
r5_gfx9_set_aperture_plus(int plus)
{
	sAperturePlus = plus ? 1 : 0;
}


void
r5_gfx9_set_hub_program(r5_gfx9_hub_fn fn)
{
	sHubProgram = fn;
}

enum { OUT = 0 };


static uint32
Read32(uint32, uint32 offset)
{
	return sHost->read32(sHost->cookie, offset);
}


static void
Write32(uint32, uint32 offset, uint32 value)
{
	sHost->write32(sHost->cookie, offset, value);
}


static void
r5_sleep(uint32 micros)
{
	sHost->sleep_us(sHost->cookie, micros);
}


#define snooze(micros) r5_sleep(micros)


#define ERROR(format, ...) \
	sHost->log(sHost->cookie, "radeon_hd: " format, ##__VA_ARGS__)


static void
r5_text_mode(const char* mode)
{
	if (sResults == NULL)
		return;
	if (mode[0] == 'w') {
		sResults->text[0] = 0;
		sResults->used = 0;
	}
}


static void
r5_text(const char* format, ...)
{
	if (sResults == NULL)
		return;
	char line[640];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	size_t bytes = strlen(line);
	if (sResults->used + bytes >= sizeof(sResults->text))
		return;
	memcpy(sResults->text + sResults->used, line, bytes + 1);
	sResults->used += bytes;
	sHost->log(sHost->cookie, "radeon_hd: %s", line);
}


static status_t
r5_vram(uint64 bytes, uint64 align, uint64* offset, uint8** address)
{
	return sHost->vram_alloc(sHost->cookie, bytes, align, offset, address);
}


static uint8*
radeon_read_named_firmware(const char* fileName, size_t* bytes)
{
	return sHost->read_firmware(sHost->cookie, fileName, bytes);
}



static const uint32 kGfx9Seg0 = GC_BASE__INST0_SEG0;
static const uint32 kGfx9Seg1 = GC_BASE__INST0_SEG1;
// gfx_v9_0_get_csb_size is 904 for this table. SET_BASE is 4 dwords,
// VGT_INDEX_TYPE is 3, and the scratch packet is 3.
static const uint32 kGfx9StartDwords = 914;
static const uint32 kPacket3Preamble = 0x4A;
static const uint32 kPacket3ContextControl = 0x28;
static const uint32 kPacket3SetContext = 0x69;
static const uint32 kPacket3ClearState = 0x12;
static const uint32 kPacket3SetBase = 0x11;
static const uint32 kPacket3SetUconfig = 0x79;
static const uint32 kContextRegStart = 0x0000a000;
static const uint32 kContextRegEnd = 0x0000a400;
static const uint32 kUconfigRegStart = 0x0000c000;
static const uint32 kPreambleBegin = 2u << 28;
static const uint32 kPreambleEnd = 3u << 28;
static const uint32 kUconfigIndexType = 2u << 28;
static const uint32 kCePartitionBase = 3;
static const uint32 kVgtIndexType = 0x2243;
static const uint32 kScratchReg0 = mmSCRATCH_REG0;
static const uint32 kType2Nop = 0x80000000;

struct radeon_gfx9_ring_info {
	uint64	ringOffset;
	uint8*	ringAddress;
	uint32	ringBytes;
	uint64	submitted;
	uint64	fenceOffset;
	uint8*	fenceAddress;
	bool	ready;
};

static radeon_gfx9_ring_info sRadeonGfx9Ring;
static bool sGfx9Workload = false;
static bool sGfx9Compute = false;

// AMDGPU_DOORBELL64_GFX_RING0 is qword 0x8b. gfx_v9_0 stores
// doorbell_index as that value shifted left by 1. The control register
// shifts it by 2 again. WDOORBELL64 indexes a u32 pointer, so the byte
// offset is 0x458.
static const uint32 kGfx9DoorbellIndex = 0x8bu << 1;
static const uint32 kGfx9DoorbellByte = kGfx9DoorbellIndex << 2;
static_assert(kGfx9StartDwords == 914, "GFX9 ring contract: 914 dwords");
static_assert(kGfx9DoorbellByte == 0x458, "GFX9 ring contract: doorbell 0x458");
static_assert(kGfx9Seg0 == GC_BASE__INST0_SEG0 && kGfx9Seg1 == GC_BASE__INST0_SEG1,
	"GFX9 ring contract: GC segments");
// vega10_ip_offset.h NBIO instance 0, segment 2, is 0xD20.
// nbio_7_0_offset.h puts these registers in that segment.
// Renoir uses nbio_v7_0. The byte address is (base + index) * 4.
// A read of 0xffffffff means this window does not decode the register.
static const uint32 kNbioDoorbellAper = (NBIO_BASE__INST0_SEG2 + mmRCC_DOORBELL_APER_EN) * 4u;
static const uint32 kNbioSdma0Range = (NBIO_BASE__INST0_SEG2 + mmBIF_SDMA0_DOORBELL_RANGE) * 4u;
static const uint32 kNbioSdma1Range = (NBIO_BASE__INST0_SEG2 + mmBIF_SDMA1_DOORBELL_RANGE) * 4u;
static const uint32 kNbioIhRange = (NBIO_BASE__INST0_SEG2 + mmBIF_IH_DOORBELL_RANGE) * 4u;
static const uint32 kNbioMmschRange = (NBIO_BASE__INST0_SEG2 + mmBIF_MMSCH0_DOORBELL_RANGE) * 4u;

static uint32
radeon_gfx9_byte(uint32 segment, uint32 index)
{
	return (segment + index) * 4;
}


static bool
radeon_mmio_fits(uint32 byteOffset)
{
	if (sHost == NULL || sHost->mmio_fits == NULL)
		return false;
	return sHost->mmio_fits(sHost->cookie, byteOffset);
}


static uint32
radeon_gfx9_halt_mask()
{
	// gfx_v9_0_cp_gfx_enable changes only CE_HALT, PFP_HALT, and ME_HALT.
	// Bits 4, 6, and 8 are icache invalidates. Bits 16-21 are pipe resets.
	return (1u << 24) | (1u << 26) | (1u << 28);
}


static uint32
radeon_packet3(uint32 opcode, uint32 count)
{
	return (3u << 30) | ((count & 0x3fffu) << 16) | ((opcode & 0xffu) << 8);
}


static bool
radeon_gfx9_put(uint32* ring, uint32 capacity, uint32* n, uint32 value)
{
	if (*n >= capacity)
		return false;
	if (ring != NULL)
		ring[*n] = value;
	*n += 1;
	return true;
}


static uint32
radeon_gfx9_ring_bytes(uint32 dwords)
{
	uint64 need = (uint64)dwords * sizeof(uint32);
	uint32 size = 4096;
	while ((uint64)size < need) {
		if (size >= 64 * 1024)
			return 0;
		size *= 2;
	}
	return size;
}


static uint32
radeon_gfx9_rb_cntl(uint32 ringBytes)
{
	uint32 qwords = ringBytes / 8;
	uint32 bufsz = 0;
	uint32 bit = 1;
	while (bit < qwords && bufsz < 31) {
		bit <<= 1;
		bufsz++;
	}
	if (bit != qwords || bufsz < 2)
		return 0;
	return bufsz | ((bufsz - 2) << 8);
}


// Walks gfx9_cs_data the way gfx_v9_0_get_csb_buffer does, then the SET_BASE
// and VGT_INDEX_TYPE packets from gfx_v9_0_cp_gfx_start, then the scratch
// packet. A NULL ring only counts. A bad extent or a count other than
// kGfx9StartDwords returns 0 so the ring is never programmed.
static uint32
radeon_gfx9_fill(uint32* ring, uint32 capacity, bool csbOnly)
{
	uint32 n = 0;
	if (!radeon_gfx9_put(ring, capacity, &n, radeon_packet3(kPacket3Preamble, 0))
		|| !radeon_gfx9_put(ring, capacity, &n, kPreambleBegin)
		|| !radeon_gfx9_put(ring, capacity, &n,
			radeon_packet3(kPacket3ContextControl, 1))
		|| !radeon_gfx9_put(ring, capacity, &n, 0x80000000)
		|| !radeon_gfx9_put(ring, capacity, &n, 0x80000000))
		return 0;

	for (const cs_section_def* sect = gfx9_cs_data; sect->section != NULL;
		++sect) {
		for (const cs_extent_def* ext = sect->section; ext->extent != NULL;
			++ext) {
			if (sect->id != SECT_CONTEXT)
				return 0;
			if (ext->reg_count == 0 || ext->reg_count > 0x3fffu)
				return 0;
			if (ext->reg_index < kContextRegStart
				|| ext->reg_index > kContextRegEnd - ext->reg_count)
				return 0;
			if (!radeon_gfx9_put(ring, capacity, &n,
					radeon_packet3(kPacket3SetContext, ext->reg_count))
				|| !radeon_gfx9_put(ring, capacity, &n,
					ext->reg_index - kContextRegStart))
				return 0;
			for (uint32 i = 0; i < ext->reg_count; i++) {
				if (!radeon_gfx9_put(ring, capacity, &n, ext->extent[i]))
					return 0;
			}
		}
	}

	uint32 vgt = (kGfx9Seg1 + kVgtIndexType) - kUconfigRegStart;
	uint32 scratch = (kGfx9Seg1 + kScratchReg0) - kUconfigRegStart;
	if (vgt >= 0x400u || scratch >= 0x400u)
		return 0;
	if (!radeon_gfx9_put(ring, capacity, &n, radeon_packet3(kPacket3Preamble, 0))
		|| !radeon_gfx9_put(ring, capacity, &n, kPreambleEnd)
		|| !radeon_gfx9_put(ring, capacity, &n,
			radeon_packet3(kPacket3ClearState, 0))
		|| !radeon_gfx9_put(ring, capacity, &n, 0))
		return 0;
	// gfx_v9_0_get_csb_buffer stops here. SET_BASE and the scratch packet
	// are submitted on the ring, not stored in the RLC clear-state buffer.
	if (csbOnly)
		return n;
	if (!radeon_gfx9_put(ring, capacity, &n, radeon_packet3(kPacket3SetBase, 2))
		|| !radeon_gfx9_put(ring, capacity, &n, kCePartitionBase)
		|| !radeon_gfx9_put(ring, capacity, &n, 0x8000)
		|| !radeon_gfx9_put(ring, capacity, &n, 0x8000)
		|| !radeon_gfx9_put(ring, capacity, &n,
			radeon_packet3(kPacket3SetUconfig, 1))
		|| !radeon_gfx9_put(ring, capacity, &n, kUconfigIndexType | vgt)
		|| !radeon_gfx9_put(ring, capacity, &n, 0)
		|| !radeon_gfx9_put(ring, capacity, &n,
			radeon_packet3(kPacket3SetUconfig, 1))
		|| !radeon_gfx9_put(ring, capacity, &n, scratch)
		|| !radeon_gfx9_put(ring, capacity, &n, 0xDEADBEEF))
		return 0;
	if (n != kGfx9StartDwords)
		return 0;
	return n;
}


static uint32
radeon_gfx9_fill_start(uint32* ring, uint32 capacity)
{
	return radeon_gfx9_fill(ring, capacity, false);
}




static void
radeon_gfx9_prepare_ring()
{
	const uint32 kFenceBytes = 4096;

	memset(&sRadeonGfx9Ring, 0, sizeof(sRadeonGfx9Ring));

	uint32 submitted = radeon_gfx9_fill_start(NULL, 0xffffffffu);
	uint32 ringBytes = radeon_gfx9_ring_bytes(submitted);
	if (submitted == 0 || ringBytes == 0 || radeon_gfx9_rb_cntl(ringBytes) == 0
		|| submitted >= ringBytes / sizeof(uint32)) {
		ERROR("%s: R5 radeon_hd: GFX9 clear state refused\n", __func__);
		return;
	}

	uint64 ringOffset = 0;
	uint8* ringAddress = NULL;
	status_t result = r5_vram(ringBytes, ringBytes, &ringOffset,
		&ringAddress);
	if (result != B_OK || ringAddress == NULL) {
		ERROR("%s: R5 radeon_hd: GFX9 ring alloc failed\n", __func__);
		return;
	}

	uint64 fenceOffset = 0;
	uint8* fenceAddress = NULL;
	result = r5_vram(kFenceBytes, kFenceBytes, &fenceOffset,
		&fenceAddress);
	if (result != B_OK || fenceAddress == NULL) {
		ERROR("%s: R5 radeon_hd: GFX9 fence alloc failed\n", __func__);
		return;
	}

	uint32* ring = (uint32*)ringAddress;
	uint32 dwords = ringBytes / sizeof(uint32);
	for (uint32 i = 0; i < dwords; i++)
		ring[i] = kType2Nop;
	if (radeon_gfx9_fill_start(ring, dwords) != submitted) {
		ERROR("%s: R5 radeon_hd: GFX9 clear state refused\n", __func__);
		return;
	}

	volatile uint32* fence = (volatile uint32*)fenceAddress;
	*fence = 0xCAFEDEAD;
	uint32 fenceRead = *fence;
	uint32 headerRead = ring[0];
	uint32 scratchIndex = (kGfx9Seg1 + kScratchReg0) - kUconfigRegStart;
	if (submitted < 3 || headerRead != 0xc0004a00
		|| ring[submitted - 3] != radeon_packet3(kPacket3SetUconfig, 1)
		|| ring[submitted - 2] != scratchIndex
		|| ring[submitted - 1] != 0xDEADBEEF) {
		ERROR("%s: R5 radeon_hd: GFX9 ring contract: stream mismatch\n",
			__func__);
		return;
	}

	sRadeonGfx9Ring.ringOffset = ringOffset;
	sRadeonGfx9Ring.ringAddress = ringAddress;
	sRadeonGfx9Ring.ringBytes = ringBytes;
	sRadeonGfx9Ring.submitted = submitted;
	sRadeonGfx9Ring.fenceOffset = fenceOffset;
	sRadeonGfx9Ring.fenceAddress = fenceAddress;
	/* amdgpu_ring_init sets *cond_exe_cpu_addr = 1 once. The packet
	 * reads kR5CondExecOffset on this same page. */
	((volatile uint32*)fenceAddress)[kR5CondExecOffset / 4u] = 1u;
	sRadeonGfx9Ring.ready = true;

	ERROR("%s: R5 radeon_hd: GFX9 ring 0x%" B_PRIx64 "+0x%" B_PRIx32
		" dwords %" B_PRIu32 " packet 0x%" B_PRIx32 " fence 0x%" B_PRIx64
		" read 0x%" B_PRIx32 "\n", __func__, ringOffset, ringBytes, submitted,
		headerRead, fenceOffset, fenceRead);
}


static uint32
radeon_le32(const uint8* bytes)
{
	return (uint32)bytes[0] | ((uint32)bytes[1] << 8)
		| ((uint32)bytes[2] << 16) | ((uint32)bytes[3] << 24);
}


static uint16
radeon_le16(const uint8* bytes)
{
	return (uint16)(bytes[0] | (bytes[1] << 8));
}


struct radeon_gfx9_ucode {
	uint8*			blob;
	const uint8*	ucode;
	uint32			ucodeBytes;
	uint32			version;
	uint32			feature;
	uint16			headerMajor;
	uint16			headerMinor;
	// gfx_firmware_header_v1_0 jt_offset and jt_size, in bytes from the
	// ucode array. Zero when the header is not 1.0 or the table is empty.
	uint32			jtOffset;
	uint32			jtBytes;
	// RLC header 2.1. Offsets are from the start of the file. Linux
	// amdgpu_gfx_rlc_init_microcode_v2_1 sends these through the PSP
	// before RLC_G. Zero when the header is not 2.1.
	uint32			listCntlBytes;
	uint32			listCntlOffset;
	uint32			listGpmBytes;
	uint32			listGpmOffset;
	uint32			listSrmBytes;
	uint32			listSrmOffset;
};

// Filled once, when the PSP load parses a header. GET_INFO copies these.
static uint32 sFwVersion[32];
static uint32 sFwFeature[32];


void
r5_gfx9_copy_firmware(uint32* version, uint32* feature, uint32 count)
{
	uint32 n = count < 32 ? count : 32;
	if (version != NULL)
		memcpy(version, sFwVersion, n * sizeof(uint32));
	if (feature != NULL)
		memcpy(feature, sFwFeature, n * sizeof(uint32));
}


static void
radeon_gfx9_note_fw(const char* name, const radeon_gfx9_ucode* fw)
{
	uint32 index = 32;
	if (name == NULL || fw == NULL)
		return;
	if (strstr(name, "_mec.bin") != NULL)
		index = 0x08;
	else if (strstr(name, "_me.bin") != NULL)
		index = 0x04;
	else if (strstr(name, "_pfp.bin") != NULL)
		index = 0x05;
	else if (strstr(name, "_ce.bin") != NULL)
		index = 0x06;
	else if (strstr(name, "_rlc.bin") != NULL)
		index = 0x07;
	if (index >= 32)
		return;
	sFwVersion[index] = fw->version;
	sFwFeature[index] = fw->feature;
}


// Set when the three RLC 2.1 lists were accepted. SRM_ENABLE follows that.
static bool sRlcRestoreLists = false;
// Both MEC items, type 4 and type 5, returned PSP status 0.
static bool sGfx9MecLoaded = false;

static status_t
radeon_gfx9_open_ucode(const char* fileName, radeon_gfx9_ucode* out)
{
	memset(out, 0, sizeof(*out));
	size_t bytes = 0;
	uint8* blob = radeon_read_named_firmware(fileName, &bytes);
	if (blob == NULL)
		return B_ENTRY_NOT_FOUND;

	status_t result = B_BAD_DATA;
	if (bytes >= 44) {
		uint32 sizeBytes = radeon_le32(blob + 0);
		uint16 headerMajor = radeon_le16(blob + 8);
		uint16 headerMinor = radeon_le16(blob + 10);
		uint16 ipMajor = radeon_le16(blob + 12);
		uint16 ipMinor = radeon_le16(blob + 14);
		uint32 version = radeon_le32(blob + 16);
		uint32 ucodeBytes = radeon_le32(blob + 20);
		uint32 offset = radeon_le32(blob + 24);
		bool headerOk = (headerMajor == 1 && headerMinor == 0)
			|| (headerMajor == 2 && headerMinor == 1);
		if (sizeBytes == bytes && headerOk && ipMajor == 9 && ipMinor == 3
			&& offset >= 32 && offset <= bytes
			&& ucodeBytes <= bytes - offset
			&& ucodeBytes >= 4 && (ucodeBytes % 4) == 0) {
			uint32 jtOffset = 0;
			uint32 jtBytes = 0;
			bool jtOk = true;
			// RLC 2.1 uses a different header after the common fields.
			// PFP, ME, and CE carry a table that is not a suffix. Only
			// the MEC submit path requires that suffix.
			if (headerMajor == 1 && headerMinor == 0) {
				uint32 jtDwords = radeon_le32(blob + 36);
				uint32 jtSize = radeon_le32(blob + 40);
				if (jtDwords != 0 || jtSize != 0) {
					if (jtSize > (0xffffffffu / 4)
						|| jtDwords > (0xffffffffu / 4))
						jtOk = false;
					else {
						jtBytes = jtSize * 4;
						jtOffset = jtDwords * 4;
						if (jtBytes == 0 || jtOffset > ucodeBytes
							|| jtBytes > ucodeBytes - jtOffset)
							jtOk = false;
					}
				}
			}
			bool listsOk = true;
			uint32 listCntlBytes = 0;
			uint32 listCntlOffset = 0;
			uint32 listGpmBytes = 0;
			uint32 listGpmOffset = 0;
			uint32 listSrmBytes = 0;
			uint32 listSrmOffset = 0;
			// rlc_firmware_header_v2_1. The three sizes sit at bytes
			// 116, 132 and 148. The offsets follow each size.
			if (headerMajor == 2 && headerMinor == 1) {
				uint32 headerBytes = radeon_le32(blob + 4);
				listsOk = headerBytes >= 156 && headerBytes <= bytes;
				if (listsOk) {
					listCntlBytes = radeon_le32(blob + 116);
					listCntlOffset = radeon_le32(blob + 120);
					listGpmBytes = radeon_le32(blob + 132);
					listGpmOffset = radeon_le32(blob + 136);
					listSrmBytes = radeon_le32(blob + 148);
					listSrmOffset = radeon_le32(blob + 152);
					listsOk = listCntlBytes >= 4
						&& (listCntlBytes % 4) == 0
						&& listGpmBytes >= 4
						&& (listGpmBytes % 4) == 0
						&& listSrmBytes >= 4
						&& (listSrmBytes % 4) == 0
						&& listCntlOffset <= bytes
						&& listCntlBytes <= bytes - listCntlOffset
						&& listGpmOffset <= bytes
						&& listGpmBytes <= bytes - listGpmOffset
						&& listSrmOffset <= bytes
						&& listSrmBytes <= bytes - listSrmOffset;
				}
			}
			if (jtOk && listsOk) {
				out->blob = blob;
				out->ucode = blob + offset;
				out->ucodeBytes = ucodeBytes;
				out->version = version;
				out->feature = radeon_le32(blob + 32);
				out->headerMajor = headerMajor;
				out->headerMinor = headerMinor;
				out->jtOffset = jtOffset;
				out->jtBytes = jtBytes;
				out->listCntlBytes = listCntlBytes;
				out->listCntlOffset = listCntlOffset;
				out->listGpmBytes = listGpmBytes;
				out->listGpmOffset = listGpmOffset;
				out->listSrmBytes = listSrmBytes;
				out->listSrmOffset = listSrmOffset;
				result = B_OK;
			}
		}
	}
	if (result != B_OK) {
		ERROR("%s: R5 radeon_hd: GFX9 firmware %s rejected\n", __func__,
			fileName);
		free(blob);
	}
	return result;
}


static status_t
radeon_gfx9_load_engine(const char* fileName, uint32 addrIndex,
	uint32 dataIndex, uint32 startAddr)
{
	radeon_gfx9_ucode fw;
	status_t result = radeon_gfx9_open_ucode(fileName, &fw);
	if (result != B_OK)
		return result;

	uint32 addr = radeon_gfx9_byte(kGfx9Seg1, addrIndex);
	uint32 data = radeon_gfx9_byte(kGfx9Seg1, dataIndex);
	if (!radeon_mmio_fits(data)) {
		free(fw.blob);
		ERROR("%s: R5 radeon_hd: GFX9 mmio does not cover %s\n",
			__func__, fileName);
		return B_ERROR;
	}
	// CP engines start at 0. gfx_v9_0_rlc_load_microcode starts at
	// RLCG_UCODE_LOADING_START_ADDRESS (0x2000).
	Write32(OUT, addr, startAddr);
	for (uint32 i = 0; i < fw.ucodeBytes; i += 4)
		Write32(OUT, data, radeon_le32(fw.ucode + i));
	Write32(OUT, addr, fw.version);
	uint32 seen = Read32(OUT, addr);
	// gfx_v9_0_cp_gfx_load_microcode writes the version and does not
	// read it back. On the 5625U this register reads 0xffffffff.
	ERROR("%s: R5 radeon_hd: GFX9 %s header %u.%u version 0x%"
		B_PRIx32 " bytes %" B_PRIu32 " addr 0x%" B_PRIx32 "\n",
		__func__, fileName, fw.headerMajor, fw.headerMinor, fw.version,
		fw.ucodeBytes, seen);
	free(fw.blob);
	// A read of 0xffffffff is the unmapped register, not a loaded engine.
	if (seen == fw.version)
		return B_OK;
	ERROR("%s: R5 radeon_hd: GFX9 firmware %s rejected\n", __func__,
		fileName);
	return B_BAD_DATA;
}


static const uint32 kPspMp0 = 0x00016000;
static const uint32 kPspRingBytes = 4096;
static const uint32 kPspCmdBytes = 1024;
// PSP_TMR_SIZE for GC 9.3 is 4 MiB. The start is naturally aligned.
static const uint32 kPspTmrBytes = 4u * 1024u * 1024u;
static_assert(kPspTmrBytes == 4u * 1024u * 1024u, "GFX9 ring contract: TMR");


static uint32
radeon_psp_byte(uint32 index)
{
	return (kPspMp0 + index) * 4;
}


static void
radeon_visible_barrier()
{
#if defined(__x86_64__) || defined(__i386__)
	asm volatile("sfence" ::: "memory");
#else
	__sync_synchronize();
#endif
}


static void
radeon_psp_store32(uint8* bytes, uint32 value)
{
	bytes[0] = (uint8)value;
	bytes[1] = (uint8)(value >> 8);
	bytes[2] = (uint8)(value >> 16);
	bytes[3] = (uint8)(value >> 24);
}


static bool
radeon_psp_wait(uint32 byteOffset, uint32 mask, uint32 value, int milliseconds,
	uint32* _seen)
{
	uint32 seen = 0;
	for (int i = 0; i < milliseconds; i++) {
		seen = Read32(OUT, byteOffset);
		if ((seen & mask) == value) {
			*_seen = seen;
			return true;
		}
		snooze(1000);
	}
	*_seen = seen;
	return false;
}


struct radeon_psp_item {
	const char*	name;
	uint32		type;
	uint32		bytes;
	uint64		offset;
	uint8*		address;
	// MEC is compute. A finished PSP refusal does not stop the GFX ring.
	bool		optional;
};


static status_t
radeon_psp_stage(const uint8* bytes, uint32 size, radeon_psp_item* item)
{
	if (bytes == NULL || size < 4 || (size % 4) != 0)
		return B_BAD_DATA;

	uint64 padded = ((uint64)size + 4095ull) & ~4095ull;
	status_t result = r5_vram(padded, 4096, &item->offset,
		&item->address);
	if (result != B_OK || item->address == NULL)
		return result == B_OK ? B_ERROR : result;

	memcpy(item->address, bytes, size);
	if (padded > size)
		memset(item->address + size, 0, (size_t)(padded - size));
	item->bytes = size;
	return B_OK;
}


static const char*
radeon_gfx9_fw_stem()
{
	// The PFP, ME, and CE blobs differ. RLC and MEC are the same bytes.
	// An id that is not in the device table is not green_sardine.
	const char* stem = r5_gfx9_fw_stem_for(sPciId);
	if (stem == NULL) {
		static int logged = 0;
		if (logged == 0) {
			logged = 1;
			ERROR("%s: R5 radeon_hd: unknown PCI id 0x%x, "
				"firmware refused\n", __func__, sPciId);
		}
	}
	return stem;
}


// gmc_v9_0_vram_gtt_location. vram_start is the firmware framebuffer
// window. vram_base_offset is the physical UMA carveout. They differ on
// Renoir and Green Sardine.
static uint64
radeon_gfx9_mc_addr(uint32 location, uint64 offset)
{
	return r5_gfx9_fb_start(location) + offset;
}


static uint64
radeon_gfx9_pa_addr(uint32 fbOffset, uint64 offset)
{
	return r5_gfx9_fb_start(fbOffset) + offset;
}


static status_t
radeon_psp_submit(uint8* ringAddress, uint8* cmdAddress, uint8* fenceAddress,
	uint32 writePtr, uint64 cmdGpu, uint64 fenceGpu, uint32 fenceValue,
	uint32* _status, uint32* _fenceSeen)
{
	uint32 wptr = Read32(OUT, writePtr);
	if (wptr >= (kPspRingBytes / 4) || (wptr % 16) != 0) {
		ERROR("%s: R5 radeon_hd: PSP wptr 0x%" B_PRIx32 "\n",
			__func__, wptr);
		return B_ERROR;
	}

	uint8* frame = ringAddress + (wptr * 4);
	memset(frame, 0, 64);
	// psp_ring_cmd_submit leaves cmd_buf_size at 0.
	radeon_psp_store32(frame + 0, (uint32)cmdGpu);
	radeon_psp_store32(frame + 4, (uint32)(cmdGpu >> 32));
	radeon_psp_store32(frame + 12, (uint32)fenceGpu);
	radeon_psp_store32(frame + 16, (uint32)(fenceGpu >> 32));
	radeon_psp_store32(frame + 20, fenceValue);
	volatile uint32* fence = (volatile uint32*)fenceAddress;
	*fence = 0;
	radeon_visible_barrier();

	uint32 next = wptr + 16;
	if (next >= (kPspRingBytes / 4))
		next = 0;
	Write32(OUT, writePtr, next);

	uint32 fenceSeen = 0;
	bool landed = false;
	for (int wait = 0; wait < 500; wait++) {
		fenceSeen = *fence;
		if (fenceSeen == fenceValue) {
			landed = true;
			break;
		}
		snooze(1000);
	}
	*_fenceSeen = fenceSeen;
	*_status = radeon_le32(cmdAddress + 864);
	if (!landed)
		return B_TIMED_OUT;
	return B_OK;
}


static status_t
radeon_psp_load_engines(uint32 location, uint32 fbOffset)
{
	const char* stem = radeon_gfx9_fw_stem();
	if (stem == NULL)
		return B_ERROR;
	char rlc[32];
	char pfp[32];
	char ce[32];
	char me[32];
	char mec[32];
	snprintf(rlc, sizeof(rlc), "%s_rlc.bin", stem);
	snprintf(pfp, sizeof(pfp), "%s_pfp.bin", stem);
	snprintf(ce, sizeof(ce), "%s_ce.bin", stem);
	snprintf(me, sizeof(me), "%s_me.bin", stem);
	snprintf(mec, sizeof(mec), "%s_mec.bin", stem);
	// GFX_FW_TYPE in psp_gfx_if.h: ME 1, PFP 2, CE 3, CP_MEC 4,
	// CP_MEC_ME1 5, RLC_G 8, GPM list 20, SRM list 21, CNTL list 22.
	// jtType 0 sends the whole ucode. A nonzero jtType splits a suffix
	// table the way amdgpu_ucode_init_single_fw does.
	// gfx_v9_0_load_mec2_fw_bin_support is false for GC 9.3.0, so
	// this path does not send CP_MEC_ME2. An RLC header 2.1 is sent as
	// the three lists and then RLC_G. Linux psp_load_non_psp_fw walks
	// enum AMDGPU_UCODE_ID: CE, PFP, ME, MEC1 and MEC1_JT, then the
	// three lists, then RLC_G last.
	struct {
		const char*	name;
		uint32		type;
		uint32		jtType;
		bool		required;
	} files[5] = {
		{ ce, 3, 0, true },
		{ pfp, 2, 0, true },
		{ me, 1, 0, true },
		{ mec, 4, 5, true },
		{ rlc, 8, 0, false },
	};

	ERROR("%s: R5 radeon_hd: PSP MEC2 not loaded on GC 9.3\n", __func__);

	sRlcRestoreLists = false;
	sGfx9MecLoaded = false;
	bool mecBodyOk = false;
	bool mecJtOk = false;
	bool listsStaged = false;
	radeon_psp_item items[12];
	uint32 count = 0;
	memset(items, 0, sizeof(items));
	for (uint32 i = 0; i < 5; i++) {
		radeon_gfx9_ucode fw;
		const char* used = files[i].name;
		status_t opened = radeon_gfx9_open_ucode(used, &fw);
		if (opened == B_ENTRY_NOT_FOUND && !files[i].required)
			continue;
		if (opened != B_OK)
			return opened;
		radeon_gfx9_note_fw(used, &fw);

		if (files[i].jtType == 0) {
			if (files[i].type == 8 && fw.headerMajor == 2
				&& fw.headerMinor == 1) {
				struct {
					const char*	name;
					uint32		type;
					uint32		bytes;
					uint32		offset;
				} lists[3] = {
					{ "rlc-cntl", 22, fw.listCntlBytes,
						fw.listCntlOffset },
					{ "rlc-gpm", 20, fw.listGpmBytes,
						fw.listGpmOffset },
					{ "rlc-srm", 21, fw.listSrmBytes,
						fw.listSrmOffset },
				};
				if (count + 4 > 12) {
					free(fw.blob);
					return B_ERROR;
				}
				for (uint32 list = 0; list < 3; list++) {
					items[count].name = lists[list].name;
					items[count].type = lists[list].type;
					items[count].optional = false;
					status_t staged = radeon_psp_stage(
						fw.blob + lists[list].offset,
						lists[list].bytes, &items[count]);
					ERROR("%s: R5 radeon_hd: PSP %s type %"
						B_PRIu32 " bytes %" B_PRIu32 "\n",
						__func__, lists[list].name,
						lists[list].type, lists[list].bytes);
					if (staged != B_OK) {
						free(fw.blob);
						return staged;
					}
					count++;
				}
				listsStaged = true;
			}
			items[count].name = used;
			items[count].type = files[i].type;
			items[count].optional = false;
			status_t staged = radeon_psp_stage(fw.ucode, fw.ucodeBytes,
				&items[count]);
			ERROR("%s: R5 radeon_hd: PSP %s header %u.%u version 0x%"
				B_PRIx32 " bytes %" B_PRIu32 "\n", __func__, used,
				fw.headerMajor, fw.headerMinor, fw.version,
				fw.ucodeBytes);
			free(fw.blob);
			if (staged != B_OK)
				return staged;
			count++;
			continue;
		}

		uint32 bodyBytes = 0;
		if (fw.jtBytes < fw.ucodeBytes)
			bodyBytes = fw.ucodeBytes - fw.jtBytes;
		// The body length is ucode_size_bytes minus the table. On
		// these MEC files that length is also jt_offset, a suffix.
		bool suffix = fw.headerMajor == 1 && fw.headerMinor == 0
			&& fw.jtBytes >= 4 && bodyBytes >= 4
			&& bodyBytes == fw.jtOffset
			&& fw.jtOffset + fw.jtBytes == fw.ucodeBytes
			&& count + 2 <= 12;
		if (!suffix) {
			free(fw.blob);
			ERROR("%s: R5 radeon_hd: GFX9 MEC %s jump table refused\n",
				__func__, used);
			return B_BAD_DATA;
		}
		ERROR("%s: R5 radeon_hd: PSP %s header %u.%u version 0x%"
			B_PRIx32 " body %" B_PRIu32 " jt %" B_PRIu32 "\n",
			__func__, used, fw.headerMajor, fw.headerMinor, fw.version,
			bodyBytes, fw.jtBytes);

		items[count].name = used;
		items[count].type = files[i].type;
		items[count].optional = true;
		status_t staged = radeon_psp_stage(fw.ucode, bodyBytes,
			&items[count]);
		if (staged != B_OK) {
			free(fw.blob);
			return staged;
		}
		count++;
		items[count].name = used;
		items[count].type = files[i].jtType;
		items[count].optional = true;
		staged = radeon_psp_stage(fw.ucode + fw.jtOffset, fw.jtBytes,
			&items[count]);
		free(fw.blob);
		if (staged != B_OK)
			return staged;
		count++;
	}

	uint64 ringOffset = 0;
	uint8* ringAddress = NULL;
	status_t result = r5_vram(kPspRingBytes, kPspRingBytes,
		&ringOffset, &ringAddress);
	if (result != B_OK || ringAddress == NULL)
		return result == B_OK ? B_ERROR : result;
	memset(ringAddress, 0, kPspRingBytes);

	uint64 cmdOffset = 0;
	uint8* cmdAddress = NULL;
	result = r5_vram(4096, 4096, &cmdOffset, &cmdAddress);
	if (result != B_OK || cmdAddress == NULL)
		return result == B_OK ? B_ERROR : result;
	memset(cmdAddress, 0, 4096);

	uint64 fenceOffset = 0;
	uint8* fenceAddress = NULL;
	result = r5_vram(4096, 4096, &fenceOffset, &fenceAddress);
	if (result != B_OK || fenceAddress == NULL)
		return result == B_OK ? B_ERROR : result;
	memset(fenceAddress, 0, 4096);
	radeon_visible_barrier();

	uint64 ringGpu = radeon_gfx9_mc_addr(location, ringOffset);
	uint64 cmdGpu = radeon_gfx9_mc_addr(location, cmdOffset);
	uint64 fenceGpu = radeon_gfx9_mc_addr(location, fenceOffset);
	ERROR("%s: R5 radeon_hd: PSP ring gpu 0x%" B_PRIx64
		" cmd 0x%" B_PRIx64 "\n", __func__, ringGpu, cmdGpu);

	const uint32 command = radeon_psp_byte(0x80);
	const uint32 writePtr = radeon_psp_byte(0x83);
	Write32(OUT, radeon_psp_byte(0x85), (uint32)ringGpu);
	Write32(OUT, radeon_psp_byte(0x86), (uint32)(ringGpu >> 32));
	Write32(OUT, radeon_psp_byte(0x87), kPspRingBytes);
	Write32(OUT, command, 0x00020000);
	snooze(20000);

	uint32 seen = 0;
	if (!radeon_psp_wait(command, 0x8000ffff, 0x80000000, 500, &seen)) {
		ERROR("%s: R5 radeon_hd: PSP ring init 0x%" B_PRIx32 "\n",
			__func__, seen);
		return B_TIMED_OUT;
	}
	ERROR("%s: R5 radeon_hd: PSP GPCOM ring ready 0x%" B_PRIx32 "\n",
		__func__, seen);

	uint32 fenceValue = 1;
	// psp_tmr_load runs before any LOAD_IP_FW. buf_phy_addr is the MC
	// address. system_phy_addr is vram_mc2pa. virt_phy_addr is bit 1.
	// A missing 4 MiB page, or a finished non-zero status, does not stop
	// the GFX loads. A fence timeout does: the GPCOM ring is wedged.
	uint64 tmrOffset = 0;
	uint8* tmrAddress = NULL;
	if (sHost->vram_claim != NULL)
		sHost->vram_claim(sHost->cookie, 1);
	status_t tmrAlloc = r5_vram(kPspTmrBytes, kPspTmrBytes, &tmrOffset,
		&tmrAddress);
	if (sHost->vram_claim != NULL)
		sHost->vram_claim(sHost->cookie, 0);
	if (tmrAlloc != B_OK || tmrAddress == NULL) {
		ERROR("%s: R5 radeon_hd: PSP SETUP_TMR skipped, 4 MiB is not"
			" in the pool\n", __func__);
	} else {
		uint64 tmrMc = radeon_gfx9_mc_addr(location, tmrOffset);
		uint64 tmrPa = radeon_gfx9_pa_addr(fbOffset, tmrOffset);
		memset(cmdAddress, 0, kPspCmdBytes);
		radeon_psp_store32(cmdAddress + 0, kPspCmdBytes);
		radeon_psp_store32(cmdAddress + 4, 1);
		radeon_psp_store32(cmdAddress + 8, 5);
		radeon_psp_store32(cmdAddress + 28, (uint32)tmrMc);
		radeon_psp_store32(cmdAddress + 32, (uint32)(tmrMc >> 32));
		radeon_psp_store32(cmdAddress + 36, kPspTmrBytes);
		radeon_psp_store32(cmdAddress + 40, 2);
		radeon_psp_store32(cmdAddress + 44, (uint32)tmrPa);
		radeon_psp_store32(cmdAddress + 48, (uint32)(tmrPa >> 32));
		uint32 tmrStatus = 0;
		uint32 tmrFence = 0;
		status_t sent = radeon_psp_submit(ringAddress, cmdAddress,
			fenceAddress, writePtr, cmdGpu, fenceGpu, fenceValue,
			&tmrStatus, &tmrFence);
		ERROR("%s: R5 radeon_hd: PSP SETUP_TMR mc 0x%" B_PRIx64
			" pa 0x%" B_PRIx64 " status 0x%" B_PRIx32
			" fence 0x%" B_PRIx32 "\n", __func__, tmrMc, tmrPa,
			tmrStatus, tmrFence);
		if (sent != B_OK)
			return sent;
		fenceValue++;
	}

	uint32 rlcCntlNow = 0xffffffff;
	uint32 rlcGen6Now = 0xffffffff;
	if (radeon_mmio_fits(radeon_gfx9_byte(kGfx9Seg1, mmRLC_GPM_GENERAL_6))) {
		rlcCntlNow = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CNTL));
		rlcGen6Now = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_GPM_GENERAL_6));
	}
	ERROR("%s: R5 radeon_hd: PSP after TMR RLC_CNTL 0x%" B_PRIx32
		" GENERAL_6 0x%" B_PRIx32 "\n", __func__, rlcCntlNow, rlcGen6Now);

	for (uint32 i = 0; i < count; i++) {
		uint64 fwGpu = radeon_gfx9_mc_addr(location, items[i].offset);
		memset(cmdAddress, 0, kPspCmdBytes);
		radeon_psp_store32(cmdAddress + 0, kPspCmdBytes);
		radeon_psp_store32(cmdAddress + 4, 1);
		radeon_psp_store32(cmdAddress + 8, 6);
		radeon_psp_store32(cmdAddress + 28, (uint32)fwGpu);
		radeon_psp_store32(cmdAddress + 32, (uint32)(fwGpu >> 32));
		radeon_psp_store32(cmdAddress + 36, items[i].bytes);
		radeon_psp_store32(cmdAddress + 40, items[i].type);

		uint32 status = 0;
		uint32 fenceSeen = 0;
		status_t sent = radeon_psp_submit(ringAddress, cmdAddress,
			fenceAddress, writePtr, cmdGpu, fenceGpu, fenceValue,
			&status, &fenceSeen);
		ERROR("%s: R5 radeon_hd: PSP %s type %" B_PRIu32
			" bytes %" B_PRIu32 " status 0x%" B_PRIx32
			" fence 0x%" B_PRIx32 "\n",
			__func__, items[i].name, items[i].type, items[i].bytes,
			status, fenceSeen);
		uint32 rlcCntlNow = 0xffffffff;
		uint32 rlcGen6Now = 0xffffffff;
		if (radeon_mmio_fits(radeon_gfx9_byte(kGfx9Seg1, mmRLC_GPM_GENERAL_6))) {
			rlcCntlNow = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CNTL));
			rlcGen6Now = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_GPM_GENERAL_6));
		}
		ERROR("%s: R5 radeon_hd: PSP after %s RLC_CNTL 0x%" B_PRIx32
			" GENERAL_6 0x%" B_PRIx32 "\n", __func__, items[i].name,
			rlcCntlNow, rlcGen6Now);
		if (sent != B_OK)
			return sent;
		fenceValue++;
		if (status == 0 && items[i].type == 4)
			mecBodyOk = true;
		if (status == 0 && items[i].type == 5)
			mecJtOk = true;
		if (status != 0) {
			// RLC, its three lists, PFP, CE, and ME stay required.
			// MEC is the compute engine; its refusal must not skip
			// the ring.
			if (!items[i].optional)
				return B_ERROR;
			ERROR("%s: R5 radeon_hd: MEC left unloaded type %"
				B_PRIu32 " status 0x%" B_PRIx32 "\n",
				__func__, items[i].type, status);
		}
	}
	sRlcRestoreLists = listsStaged;
	sGfx9MecLoaded = mecBodyOk && mecJtOk;
	return B_OK;
}


static bool radeon_gfx9_program_csib(uint32 location);


static bool
radeon_gfx9_rlc_halt(uint32* _before, uint32* _clock)
{
	const uint32 cntl = radeon_gfx9_byte(kGfx9Seg1, mmRLC_CNTL);
	const uint32 clock = radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGCG_CGLS_CTRL);
	*_before = 0;
	*_clock = 0;
	if (!radeon_mmio_fits(cntl))
		return false;

	uint32 saved = Read32(OUT, cntl);
	uint32 savedClock = 0;
	if (radeon_mmio_fits(clock))
		savedClock = Read32(OUT, clock);
	*_before = saved;
	*_clock = savedClock;
	// gfx_v9_0_rlc_stop clears clock gating and F32 before a new RLC image
	// is loaded. LOAD_IP_FW while RLC_CNTL bit 0 is set leaves the firmware
	// copy running.
	if (radeon_mmio_fits(clock))
		Write32(OUT, clock, 0);
	Write32(OUT, cntl, saved & ~1u);
	snooze(50);
	const uint32 cuBusy = radeon_gfx9_byte(kGfx9Seg1, mmRLC_SERDES_CU_MASTER_BUSY);
	const uint32 noncuBusy = radeon_gfx9_byte(kGfx9Seg1, mmRLC_SERDES_NONCU_MASTER_BUSY);
	const uint32 noncuMask = 0x0000ffffu | 0x00010000u | 0x00040000u
		| 0x00080000u;
	uint32 cu = 0xffffffff;
	uint32 noncu = 0xffffffff;
	bool quiet = false;
	if (radeon_mmio_fits(noncuBusy)) {
		for (int wait = 0; wait < 100; wait++) {
			cu = Read32(OUT, cuBusy);
			noncu = Read32(OUT, noncuBusy);
			if (cu == 0 && (noncu & noncuMask) == 0) {
				quiet = true;
				break;
			}
			snooze(1000);
		}
	}
	uint32 after = Read32(OUT, cntl);
	ERROR("%s: R5 radeon_hd: RLC halted before PSP 0x%" B_PRIx32
		" -> 0x%" B_PRIx32 " serdes cu 0x%" B_PRIx32 " noncu 0x%"
		B_PRIx32 " %s\n", __func__, saved, after, cu, noncu,
		quiet ? "quiet" : "busy");
	return true;
}


static uint32 sGfx9Gen6Before = 0;
static uint32 sGfx9Gen6Cleared = 0;
static uint32 sGfx9Gen6Early = 0;
static uint32 sGfx9Gen6Late = 0;


static bool
radeon_gfx9_rlc_start(uint32 location, bool loadUcode, uint32* _before,
	uint32* _clock, uint32* _after, uint32* _version)
{
	const uint32 cntl = radeon_gfx9_byte(kGfx9Seg1, mmRLC_CNTL);
	const uint32 clock = radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGCG_CGLS_CTRL);
	const uint32 version = radeon_gfx9_byte(kGfx9Seg1, mmRLC_GPM_GENERAL_6);
	*_before = 0;
	*_clock = 0;
	*_after = 0;
	*_version = 0;
	if (!radeon_mmio_fits(version)) {
		ERROR("%s: R5 radeon_hd: GFX9 mmio does not cover RLC\n",
			__func__);
		return false;
	}

	uint32 saved = Read32(OUT, cntl);
	uint32 savedClock = Read32(OUT, clock);
	*_before = saved;
	*_clock = savedClock;
	// gfx_v9_0_rlc_resume disables CGCG, halts F32, then starts it.
	Write32(OUT, clock, 0);
	Write32(OUT, cntl, saved & ~1u);
	snooze(50);
	// gfx_v9_0_rlc_stop waits for the serdes after F32 is halted.
	// Barcelo has one shader engine. The current GRBM index is left
	// alone: a wrong SE select is worse than reading that engine.
	// mmRLC_SERDES_CU_MASTER_BUSY is 0x4c61 and NONCU is 0x4c62.
	// The non-CU mask is SE, GC, TC0, and TC1.
	const uint32 cuBusy = radeon_gfx9_byte(kGfx9Seg1, mmRLC_SERDES_CU_MASTER_BUSY);
	const uint32 noncuBusy = radeon_gfx9_byte(kGfx9Seg1, mmRLC_SERDES_NONCU_MASTER_BUSY);
	const uint32 noncuMask = 0x0000ffffu | 0x00010000u | 0x00040000u
		| 0x00080000u;
	uint32 cu = 0xffffffff;
	uint32 noncu = 0xffffffff;
	bool quiet = false;
	if (radeon_mmio_fits(noncuBusy)) {
		for (int wait = 0; wait < 100; wait++) {
			cu = Read32(OUT, cuBusy);
			noncu = Read32(OUT, noncuBusy);
			if (cu == 0 && (noncu & noncuMask) == 0) {
				quiet = true;
				break;
			}
			snooze(1000);
		}
	}
	ERROR("%s: R5 radeon_hd: RLC serdes cu 0x%" B_PRIx32
		" noncu 0x%" B_PRIx32 " %s\n", __func__, cu, noncu,
		quiet ? "quiet" : "busy");
	// gfx_v9_0_init_pg programs the clear-state buffer while F32 is stopped.
	// The direct loader then writes RLC ucode. PSP has already done that.
	if (!radeon_gfx9_program_csib(location)) {
		Write32(OUT, cntl, saved);
		Write32(OUT, clock, savedClock);
		ERROR("%s: R5 radeon_hd: GFX9 clear-state buffer refused\n",
			__func__);
		return false;
	}
	if (loadUcode) {
		char rlc[32];
		const char* stem = radeon_gfx9_fw_stem();
		if (stem == NULL) {
			Write32(OUT, cntl, saved);
			Write32(OUT, clock, savedClock);
			return false;
		}
		snprintf(rlc, sizeof(rlc), "%s_rlc.bin", stem);
		// mmRLC_GPM_UCODE_ADDR / DATA. Start address is 0x2000.
		status_t loaded = radeon_gfx9_load_engine(rlc, 0x583c, 0x583d,
			0x2000);
		if (loaded != B_OK) {
			Write32(OUT, cntl, saved);
			Write32(OUT, clock, savedClock);
			ERROR("%s: R5 radeon_hd: GFX9 RLC ucode refused\n", __func__);
			return false;
		}
	}
	// RLC firmware writes its ucode version here when it starts
	// (gfx_v9_0_rlc_start, AMDGPU_RLC_DEBUG_RETRY). Cleared so the
	// value after the start comes from the image that is running.
	uint32 gen6Before = Read32(OUT, version);
	Write32(OUT, version, 0);
	uint32 gen6Cleared = Read32(OUT, version);
	Write32(OUT, cntl, (saved & ~1u) | 1u);
	snooze(50);
	uint32 after = Read32(OUT, cntl);
	uint32 seenVersion = Read32(OUT, version);
	*_after = after;
	*_version = seenVersion;
	ERROR("%s: R5 radeon_hd: RLC_CNTL 0x%" B_PRIx32 " -> 0x%" B_PRIx32
		" RLC_GPM_GENERAL_6 0x%" B_PRIx32 "\n", __func__, saved, after,
		seenVersion);
	snooze(10000);
	uint32 gen6Late = Read32(OUT, version);
	ERROR("%s: R5 radeon_hd: RLC GENERAL_6 before 0x%" B_PRIx32
		" cleared 0x%" B_PRIx32 " 50us 0x%" B_PRIx32 " 10ms 0x%" B_PRIx32
		"\n", __func__, gen6Before, gen6Cleared, seenVersion, gen6Late);
	sGfx9Gen6Before = gen6Before;
	sGfx9Gen6Cleared = gen6Cleared;
	sGfx9Gen6Early = seenVersion;
	sGfx9Gen6Late = gen6Late;
	if ((after & 1u) == 0) {
		Write32(OUT, cntl, saved);
		Write32(OUT, clock, savedClock);
		ERROR("%s: R5 radeon_hd: RLC enable did not stick\n", __func__);
		return false;
	}
	return true;
}


// Result of radeon_smu12_mode2_reset. publish writes these into
// radeon-gfx.txt. before is the mailbox response seen before the command.
// after is the response after it, or the last read on a timeout.
static bool sSoc15ResetSent = false;
static uint32 sSoc15ResetBefore = 0;
static uint32 sSoc15ResetAfter = 0;
static uint32 sGfx9FbLocation = 0;
static uint32 sGfx9FbTop = 0;
static uint32 sGfx9FbOffset = 0;
static uint64 sGfx9RingMc = 0;
static uint64 sGfx9RingPa = 0;
static uint32 sGfx9L1Before = 0;
static uint32 sGfx9L1After = 0;
static uint32 sGfx9L2Before = 0;
static uint32 sGfx9L2After = 0;


static void
radeon_gfx9_publish(bool psp, bool rlc, uint32 rlcBefore, uint32 rlcAfter,
	uint32 rlcVersion, uint32 scratch, bool packet, bool programmed,
	uint32 dwords, uint32 rptr, uint32 rptrBefore)
{
	r5_text_mode("w");
	r5_text("device %04x\n", sPciId);
	r5_text("psp %s\n", psp ? "yes" : "no");
	r5_text("rlc %s\n", rlc ? "yes" : "no");
	r5_text("rlc_before %08x\n", rlcBefore);
	r5_text("rlc_after %08x\n", rlcAfter);
	r5_text("rlc_version %08x\n", rlcVersion);
	r5_text("scratch %08x\n", scratch);
	r5_text("packet %s\n", packet ? "yes" : "no");
	r5_text("mode2 %s\n", sSoc15ResetSent ? "yes" : "no");
	r5_text("mode2_before %08x\n", sSoc15ResetBefore);
	r5_text("mode2_after %08x\n", sSoc15ResetAfter);
	r5_text("fb_location %08x\n", sGfx9FbLocation);
	r5_text("fb_top %08x\n", sGfx9FbTop);
	r5_text("fb_offset %08x\n", sGfx9FbOffset);
	r5_text("ring_mc %016" B_PRIx64 "\n", sGfx9RingMc);
	r5_text("ring_pa %016" B_PRIx64 "\n", sGfx9RingPa);
	r5_text("l1_tlb %08x %08x\n", sGfx9L1Before, sGfx9L1After);
	r5_text("l2_cntl %08x %08x\n", sGfx9L2Before, sGfx9L2After);
	r5_text("rlc_gen6 %08x %08x %08x %08x\n", sGfx9Gen6Before,
		sGfx9Gen6Cleared, sGfx9Gen6Early, sGfx9Gen6Late);
	if (programmed) {
		r5_text("dwords %u\n", dwords);
		r5_text("rptr_before %08x\n", rptrBefore);
		r5_text("rptr %08x\n", rptr);
		r5_text("wptr %08x\n", dwords);
	}
}



static void
radeon_gfx9_flush_visible(uint8* address, uint32 bytes)
{
	volatile uint8* bytesView = address;
	for (uint32 i = 0; i < bytes; i += 64)
		(void)bytesView[i];
}


// gfx_v9_0_init_golden_registers for GC 9.3 writes this list and returns.
// mmGCEA_PROBE_MAP is 0x070c with base index 0 in Linux v6.12 gfx_v9_0.c.
// golden_settings_gc_9_1_rn programs it with mask 0xffffffff and value
// 0x0000cccc. soc15_program_register_sequence uses the and mask.
struct radeon_gfx9_golden {
	uint32	segment;
	uint32	index;
	uint32	andMask;
	uint32	orMask;
};

static const radeon_gfx9_golden kGfx9Golden[] = {
	{ 0x2000, 0x0680, 0xfffdf3cf, 0x00014104 },
	{ 0x2000, 0x0682, 0xff7fffff, 0x0a000000 },
	{ 0x2000, 0x060d, 0xf00fffff, 0x00000400 },
	{ 0x2000, 0x063e, 0xf3e777ff, 0x24000042 },
	{ 0x2000, 0x0642, 0xf3e777ff, 0x24000042 },
	{ 0x2000, 0x02fc, 0x3fffffff, 0x00000001 },
	{ 0x2000, 0x02fd, 0xffffffff, 0x04040000 },
	{ 0xA000, 0x2281, 0x0000ff0f, 0x00000000 },
	{ 0x2000, 0x0542, 0xfffffeef, 0x010b0000 },
	{ 0x2000, 0x0b04, 0xffffffff, 0x00000000 },
	{ 0x2000, 0x0b03, 0xffffffff, 0x00003120 },
	{ 0x2000, 0x070c, 0xffffffff, 0x0000cccc },
};

static_assert(sizeof(kGfx9Golden) / sizeof(kGfx9Golden[0]) == 12,
	"GFX9 ring contract: golden save slot count");


struct radeon_gfx9_saved {
	bool	armed;
	uint32	fbBase;
	uint32	fbTop;
	uint32	aperLow;
	uint32	aperHigh;
	uint32	defLo;
	uint32	defHi;
	uint32	l1Tlb;
	uint32	l2Cntl;
	uint32	l2Cntl2;
	uint32	l2Cntl3;
	uint32	l2;
	uint32	grbm;
	uint32	mgcg;
	uint32	rlcMem;
	uint32	cpMem;
	uint32	pg;
	uint32	cgcg3d;
	uint32	csibLo;
	uint32	csibHi;
	uint32	csibLen;
	uint32	srm;
	uint32	mec;
	uint32	golden[12];
};

static radeon_gfx9_saved sRadeonGfx9Saved;


static bool
radeon_gfx9_save_hw()
{
	const uint32 highest = radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGCG_CGLS_CTRL_3D);
	if (!radeon_mmio_fits(highest))
		return false;

	memset(&sRadeonGfx9Saved, 0, sizeof(sRadeonGfx9Saved));
	sRadeonGfx9Saved.fbBase = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_BASE));
	sRadeonGfx9Saved.fbTop = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_TOP));
	sRadeonGfx9Saved.aperLow = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_LOW_ADDR));
	sRadeonGfx9Saved.aperHigh = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR));
	sRadeonGfx9Saved.defLo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB));
	sRadeonGfx9Saved.defHi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB));
	sRadeonGfx9Saved.l1Tlb = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_MX_L1_TLB_CNTL));
	sRadeonGfx9Saved.l2Cntl = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL));
	sRadeonGfx9Saved.l2Cntl2 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL2));
	sRadeonGfx9Saved.l2Cntl3 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL3));
	sRadeonGfx9Saved.l2 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL4));
	sRadeonGfx9Saved.grbm = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmGRBM_CNTL));
	sRadeonGfx9Saved.mgcg = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGTT_MGCG_OVERRIDE));
	sRadeonGfx9Saved.rlcMem = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_MEM_SLP_CNTL));
	sRadeonGfx9Saved.cpMem = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_MEM_SLP_CNTL));
	sRadeonGfx9Saved.pg = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_PG_CNTL));
	sRadeonGfx9Saved.cgcg3d = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGCG_CGLS_CTRL_3D));
	sRadeonGfx9Saved.csibLo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CSIB_ADDR_LO));
	sRadeonGfx9Saved.csibHi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CSIB_ADDR_HI));
	sRadeonGfx9Saved.csibLen = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CSIB_LENGTH));
	sRadeonGfx9Saved.srm = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_SRM_CNTL));
	sRadeonGfx9Saved.mec = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_MEC_CNTL));
	for (uint32 i = 0; i < sizeof(kGfx9Golden) / sizeof(kGfx9Golden[0]); i++) {
		sRadeonGfx9Saved.golden[i] = Read32(OUT,
			radeon_gfx9_byte(kGfx9Golden[i].segment, kGfx9Golden[i].index));
	}
	sRadeonGfx9Saved.armed = true;
	return true;
}


static void radeon_gfx9_saved_restore();


// CSIB and SRM are restored while RLC F32 is stopped. The enable bit goes
// back afterwards. restoreRlc writes the pair saved around rlc_start.
// Without that pair, an enable bit that was already set is put back.
static void
radeon_gfx9_restore_saved(uint32 rlcCntl, bool restoreRlc, uint32 rlcClock)
{
	const uint32 cntl = radeon_gfx9_byte(kGfx9Seg1, mmRLC_CNTL);
	const uint32 clock = radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGCG_CGLS_CTRL);
	bool stopped = false;
	if (radeon_mmio_fits(cntl)) {
		uint32 now = Read32(OUT, cntl);
		if ((now & 1u) != 0) {
			Write32(OUT, cntl, now & ~1u);
			snooze(50);
			stopped = true;
		}
	}
	radeon_gfx9_saved_restore();
	if (!stopped && !restoreRlc)
		return;
	if (!radeon_mmio_fits(cntl))
		return;
	if (restoreRlc) {
		Write32(OUT, cntl, rlcCntl);
		if (radeon_mmio_fits(clock))
			Write32(OUT, clock, rlcClock);
		return;
	}
	uint32 back = Read32(OUT, cntl);
	Write32(OUT, cntl, back | 1u);
}


static void
radeon_gfx9_saved_restore()
{
	if (!sRadeonGfx9Saved.armed)
		return;
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_BASE), sRadeonGfx9Saved.fbBase);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_TOP), sRadeonGfx9Saved.fbTop);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_LOW_ADDR), sRadeonGfx9Saved.aperLow);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR), sRadeonGfx9Saved.aperHigh);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB), sRadeonGfx9Saved.defLo);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB), sRadeonGfx9Saved.defHi);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_MX_L1_TLB_CNTL), sRadeonGfx9Saved.l1Tlb);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL), sRadeonGfx9Saved.l2Cntl);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL2), sRadeonGfx9Saved.l2Cntl2);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL3), sRadeonGfx9Saved.l2Cntl3);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL4), sRadeonGfx9Saved.l2);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmGRBM_CNTL), sRadeonGfx9Saved.grbm);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGTT_MGCG_OVERRIDE), sRadeonGfx9Saved.mgcg);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_MEM_SLP_CNTL), sRadeonGfx9Saved.rlcMem);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_MEM_SLP_CNTL), sRadeonGfx9Saved.cpMem);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_PG_CNTL), sRadeonGfx9Saved.pg);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGCG_CGLS_CTRL_3D), sRadeonGfx9Saved.cgcg3d);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CSIB_ADDR_LO), sRadeonGfx9Saved.csibLo);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CSIB_ADDR_HI), sRadeonGfx9Saved.csibHi);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CSIB_LENGTH), sRadeonGfx9Saved.csibLen);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_SRM_CNTL), sRadeonGfx9Saved.srm);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_MEC_CNTL), sRadeonGfx9Saved.mec);
	for (uint32 i = 0; i < sizeof(kGfx9Golden) / sizeof(kGfx9Golden[0]); i++) {
		Write32(OUT, radeon_gfx9_byte(kGfx9Golden[i].segment,
			kGfx9Golden[i].index), sRadeonGfx9Saved.golden[i]);
	}
	sRadeonGfx9Saved.armed = false;
}


static void radeon_gfx9_linux_golden();
static void radeon_gfx9_common_golden();


// gfxhub_v1_0_init_system_aperture_regs for Renoir and Green Sardine. The
// system aperture is in 256 KiB pages of the MC window. That window is
// MC_VM_FB_LOCATION_BASE << 24 through the firmware top. This only widens
// the aperture. It does not write FB_LOCATION and it does not install the
// MI300 page table. The default address is amdgpu_gmc_vram_mc2pa of a
// scratch page: MC_VM_FB_OFFSET << 24 plus the byte offset.
static bool
radeon_gfx9_prepare_hw(uint32 location, uint32 fbOffset)
{
	const uint32 highest = radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGCG_CGLS_CTRL_3D);
	uint64 frame = 0;
	uint64 fbEnd = 0;
	r5_gfx9_fb_range(location, sVramBytes, &frame, &fbEnd);
	if (!radeon_mmio_fits(highest) || sVramBytes < 0x1000
		|| fbEnd < frame || (fbEnd >> 18) >= 0xffffffffull)
		return false;

	// The fence page stays out of the fault redirect.
	uint64 scratchOffset = 0;
	uint8* scratchAddress = NULL;
	if (r5_vram(4096, 4096, &scratchOffset, &scratchAddress) != B_OK
		|| scratchAddress == NULL)
		return false;
	memset(scratchAddress, 0, 4096);
	radeon_visible_barrier();

	if (!sRadeonGfx9Saved.armed && !radeon_gfx9_save_hw())
		return false;

	uint32 lowNeed = (uint32)(frame >> 18);
	/* K0 always adds 1. Linux adds it only for RAVEN2, RENOIR or
	 * GREEN_SARDINE (amdgpu_device.c:2139, gfxhub_v1_0.c). */
	uint32 highNeed = (uint32)(fbEnd >> 18) + 1u;
	uint32 aperLow = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_LOW_ADDR));
	uint32 aperHigh = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR));
	uint32 lowNow = aperLow;
	uint32 highNow = aperHigh;
	/* Linux writes the AGP aperture in gfxhub_v1_0_gart_enable, before
	 * any ring. Off leaves the widen below, which is the K0 path. */
	if (sHubAperture && sHubProgram != NULL) {
		struct r5_agp_aperture agp = r5_gfx9_agp_aperture(frame, fbEnd,
			sAperturePlus);
		sHubProgram(frame, fbEnd, aperLow, aperHigh);
		lowNow = agp.low;
		highNow = agp.high;
	} else {
		if (aperLow > lowNeed) {
			Write32(OUT, radeon_gfx9_byte(kGfx9Seg0,
				mmMC_VM_SYSTEM_APERTURE_LOW_ADDR), lowNeed);
			lowNow = lowNeed;
		}
		if (aperHigh < highNeed) {
			Write32(OUT, radeon_gfx9_byte(kGfx9Seg0,
				mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR), highNeed);
			highNow = highNeed;
		}
	}

	uint64 scratchPa = radeon_gfx9_pa_addr(fbOffset, scratchOffset);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB),
		(uint32)(scratchPa >> 12));
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB),
		(uint32)(scratchPa >> 44));

	// gfxhub_v1_0_init_tlb_regs and gfxhub_v1_0_init_cache_regs. The
	// mode-2 reset leaves the GFXHUB walker off. SYSTEM_ACCESS_MODE 3
	// is the aperture check. MTYPE_UC is 3. Renoir is not
	// translate_further, so VM_L2_CNTL3 keeps mmVM_L2_CNTL3_DEFAULT
	// and sets BANK_SELECT 9 and L2_CACHE_BIGK_FRAGMENT_SIZE 6.
	uint32 l1Before = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_MX_L1_TLB_CNTL));
	uint32 l1After = (l1Before & ~0x00003879u) | 0x00003859u;
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_MX_L1_TLB_CNTL), l1After);
	uint32 l2Before = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL));
	uint32 l2After = l2Before | (1u << 0) | (1u << 1);
	l2After &= ~((1u << 8) | (1u << 18) | 0x03e00000u);
	l2After = (l2After & ~0x00180000u) | (1u << 19);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL), l2After);
	uint32 l2Cntl2 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL2));
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL2), l2Cntl2 | (1u << 0)
		| (1u << 1));
	uint32 l2Cntl3 = 0x80100007u;
	l2Cntl3 = (l2Cntl3 & ~0x0000003fu) | 9u;
	l2Cntl3 = (l2Cntl3 & ~0x000f8000u) | (6u << 15);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL3), l2Cntl3);
	sGfx9L1Before = l1Before;
	sGfx9L1After = l1After;
	sGfx9L2Before = l2Before;
	sGfx9L2After = l2After;
	ERROR("%s: R5 radeon_hd: GFX9 L1_TLB 0x%" B_PRIx32 " -> 0x%"
		B_PRIx32 " L2_CNTL 0x%" B_PRIx32 " -> 0x%" B_PRIx32 "\n",
		__func__, l1Before, l1After, l2Before, l2After);
	// Not an XGMI-to-CPU part: the walker does not force physical requests.
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL4),
		sRadeonGfx9Saved.l2 & ~((1u << 6) | (1u << 7)));

	uint32 grbm = (sRadeonGfx9Saved.grbm & ~0xffu) | 0xffu;
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmGRBM_CNTL), grbm);
	// gfx_v9_0_update_gfx_clock_gating holds these clocks when gating is off.
	uint32 mgcg = sRadeonGfx9Saved.mgcg | (1u << 0) | (1u << 1) | (1u << 2)
		| (1u << 5) | (1u << 6);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGTT_MGCG_OVERRIDE), mgcg);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_MEM_SLP_CNTL),
		sRadeonGfx9Saved.rlcMem & ~1u);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_MEM_SLP_CNTL),
		sRadeonGfx9Saved.cpMem & ~1u);
	// Gating stays off. Bits 0, 2, 3 and 4 are the GFX and per-CU enables.
	// gfx_v9_0_enable_cp_power_gating(false) sets CP_PG_DISABLE, bit 15.
	// The jump table and the SMU handshake stay unset: those belong to
	// the gfxoff path, and this driver does not send GFXOFF.
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_PG_CNTL),
		(sRadeonGfx9Saved.pg & ~((1u << 0) | (1u << 2) | (1u << 3) | (1u << 4)))
			| (1u << 15));
	// gfx_v9_0_update_3d_clock_gating clears CGCG_EN and CGLS_EN only.
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CGCG_CGLS_CTRL_3D),
		sRadeonGfx9Saved.cgcg3d & ~((1u << 0) | (1u << 1)));
	// No compute ring is programmed. Leave MEC ME1 and ME2 halted.
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_MEC_CNTL),
		sRadeonGfx9Saved.mec | (1u << 28) | (1u << 30));

	for (uint32 i = 0; i < sizeof(kGfx9Golden) / sizeof(kGfx9Golden[0]); i++) {
		uint32 reg = radeon_gfx9_byte(kGfx9Golden[i].segment,
			kGfx9Golden[i].index);
		uint32 value = kGfx9Golden[i].orMask;
		if (kGfx9Golden[i].andMask != 0xffffffffu) {
			value = (sRadeonGfx9Saved.golden[i] & ~kGfx9Golden[i].andMask)
				| (kGfx9Golden[i].orMask & kGfx9Golden[i].andMask);
		}
		Write32(OUT, reg, value);
	}
	{
		const uint32 probe = (uint32)(sizeof(kGfx9Golden)
			/ sizeof(kGfx9Golden[0]) - 1);
		uint32 probeReg = radeon_gfx9_byte(kGfx9Golden[probe].segment,
			kGfx9Golden[probe].index);
		ERROR("%s: R5 radeon_hd: GFX9 golden GCEA_PROBE_MAP 0x%" B_PRIx32
			" -> 0x%" B_PRIx32 "\n", __func__,
			sRadeonGfx9Saved.golden[probe], Read32(OUT, probeReg));
	}
	radeon_gfx9_linux_golden();
	radeon_gfx9_common_golden();

	ERROR("%s: R5 radeon_hd: GFX9 aperture low 0x%" B_PRIx32
		" -> 0x%" B_PRIx32 " high 0x%" B_PRIx32 " -> 0x%" B_PRIx32
		" default pa 0x%" B_PRIx64 "\n",
		__func__, aperLow, lowNow, aperHigh, highNow, scratchPa);
	return true;
}


/* golden_settings_gc_9_1_rn in Linux gfx_v9_0.c. The live values are
 * the 6 Nov 2026 Renoir capture (watch-2-after-preamble): CP_DEBUG
 * gc0 0x107f, SPI_CONFIG_CNTL / _1 gc1 0x2440 / 0x2441. Read first.
 * Write only a register that differs, then read it again. This is not
 * part of the twelve-slot golden save, and it does not touch VMID 0. */
static void
radeon_gfx9_linux_golden()
{
	const uint32 cpDebug = radeon_gfx9_byte(kGfx9Seg0, mmCP_DEBUG);
	const uint32 spi = radeon_gfx9_byte(kGfx9Seg1, mmSPI_CONFIG_CNTL);
	const uint32 spi1 = radeon_gfx9_byte(kGfx9Seg1, mmSPI_CONFIG_CNTL_1);
	const uint32 wantDebug = 0x00400000u;
	const uint32 wantSpi = 0x0062c688u;
	const uint32 wantSpi1 = 0x01000104u;
	if (!radeon_mmio_fits(cpDebug) || !radeon_mmio_fits(spi1))
		return;
	uint32 debug = Read32(OUT, cpDebug);
	uint32 spiNow = Read32(OUT, spi);
	uint32 spi1Now = Read32(OUT, spi1);
	ERROR("%s: R5 radeon_hd: CP_DEBUG %08" B_PRIx32
		" SPI_CONFIG_CNTL %08" B_PRIx32 " SPI_CONFIG_CNTL_1 %08" B_PRIx32
		"\n", __func__, debug, spiNow, spi1Now);
	if (debug == wantDebug && spiNow == wantSpi && spi1Now == wantSpi1)
		return;
	if (debug != wantDebug)
		Write32(OUT, cpDebug, wantDebug);
	if (spiNow != wantSpi)
		Write32(OUT, spi, wantSpi);
	if (spi1Now != wantSpi1)
		Write32(OUT, spi1, wantSpi1);
	ERROR("%s: R5 radeon_hd: golden_settings_gc_9_1_rn CP_DEBUG %08"
		B_PRIx32 " SPI_CONFIG_CNTL %08" B_PRIx32
		" SPI_CONFIG_CNTL_1 %08" B_PRIx32 "\n", __func__,
		Read32(OUT, cpDebug), Read32(OUT, spi), Read32(OUT, spi1));
}


/* Linux 6.12 gfx_v9_0_init_golden_registers returns before
 * golden_settings_gc_9_x_common on IP 9.3.0. The comment there is
 * "for renoir, don't need common goldensetting". This step is after
 * the K golden list and the CP_DEBUG/SPI step. It reads the three
 * common registers and does not write them. */
static void
radeon_gfx9_common_golden()
{
	const uint32 sd = radeon_gfx9_byte(kGfx9Seg0, mmCP_SD_CNTL);
	const uint32 index = radeon_gfx9_byte(kGfx9Seg1, mmGRBM_CAM_INDEX);
	const uint32 data = radeon_gfx9_byte(kGfx9Seg1, mmGRBM_CAM_DATA);
	if (!radeon_mmio_fits(data))
		return;
	ERROR("%s: R5 radeon_hd: gc 9.3 skips golden_settings_gc_9_x_common"
		" CP_SD_CNTL %08" B_PRIx32 " GRBM_CAM_INDEX %08" B_PRIx32
		" GRBM_CAM_DATA %08" B_PRIx32 "\n",
		__func__, Read32(OUT, sd), Read32(OUT, index), Read32(OUT, data));
}


static bool
radeon_gfx9_program_csib(uint32 location)
{
	uint32 dwords = radeon_gfx9_fill(NULL, 0xffffffffu, true);
	if (dwords == 0 || dwords + 10 != kGfx9StartDwords)
		return false;

	uint64 offset = 0;
	uint8* address = NULL;
	status_t result = r5_vram(4096, 4096, &offset, &address);
	if (result != B_OK || address == NULL)
		return false;
	memset(address, 0, 4096);
	if (radeon_gfx9_fill((uint32*)address, 4096 / sizeof(uint32), true)
		!= dwords)
		return false;
	radeon_gfx9_flush_visible(address, 4096);
	radeon_visible_barrier();

	uint64 gpu = radeon_gfx9_mc_addr(location, offset);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CSIB_ADDR_HI), (uint32)(gpu >> 32));
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CSIB_ADDR_LO), (uint32)gpu & ~3u);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CSIB_LENGTH), dwords);
	// gfx_v9_0_enable_save_restore_machine runs for every RLC 2.1 after
	// the PSP has the three lists. Without those lists the bit stays off.
	uint32 srm = sRadeonGfx9Saved.srm;
	if (sRlcRestoreLists)
		srm |= 1u;
	else
		srm &= ~1u;
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_SRM_CNTL), srm);
	ERROR("%s: R5 radeon_hd: GFX9 CSIB 0x%" B_PRIx64 " dwords %" B_PRIu32
		" srm 0x%" B_PRIx32 "\n", __func__, gpu, dwords, srm);
	return true;
}


static void
radeon_gfx9_note_doorbell(bool rang, uint32 control, uint64 poll,
	uint32 rptrWb, uint32 rptrAtBell, uint32 rptrWbAtBell,
	uint32 aperBefore, uint32 aperAfter, uint32 sdma0, uint32 sdma1,
	uint32 ihRange, uint32 mmsch)
{
	r5_text_mode("a");
	r5_text("doorbell %s\n", rang ? "yes" : "no");
	r5_text("doorbell_ctl %08x\n", control);
	r5_text("poll %016" B_PRIx64 "\n", poll);
	r5_text("rptr_at_bell %08x\n", rptrAtBell);
	r5_text("rptr_wb_at_bell %08x\n", rptrWbAtBell);
	r5_text("rptr_wb %08x\n", rptrWb);
	r5_text("aperture_before %08x\n", aperBefore);
	r5_text("aperture_after %08x\n", aperAfter);
	r5_text("sdma0_range %08x\n", sdma0);
	r5_text("sdma1_range %08x\n", sdma1);
	r5_text("ih_range %08x\n", ihRange);
	r5_text("mmsch_range %08x\n", mmsch);
}





struct radeon_gfx9_stall {
	uint32	grbm;
	uint32	grbm2;
	uint32	cp;
	uint32	cpc;
	uint32	cpInt;
	uint32	vm;
	uint32	vmLo;
	uint32	vmHi;
	uint32	safe;
	uint32	gfxCntl;
	uint32	shMem;
	uint32	cpfStatus;
	uint32	cpfBusy;
	uint32	cpfStalled;
	uint32	stalled1;
	uint32	stalled2;
	uint32	busyStat;
	uint32	pfpHeader;
	uint32	roqRbStat;
	bool	valid;
};

static radeon_gfx9_stall sGfx9Stall;


static uint32
radeon_gfx9_read_reg(uint32 segment, uint32 index)
{
	uint32 byte = radeon_gfx9_byte(segment, index);
	if (!radeon_mmio_fits(byte))
		return 0xffffffff;
	return Read32(OUT, byte);
}




static void
radeon_gfx9_capture_stall()
{
	// Offsets are gc_9_0 mm register indexes. CP_STAT bit 14 is the VM
	// walker, bit 15 the PFP, bit 17 the ME. CP_INT_STATUS bit 24 is an
	// opcode error and bit 23 a privileged register.
	memset(&sGfx9Stall, 0, sizeof(sGfx9Stall));
	sGfx9Stall.valid = true;
	sGfx9Stall.grbm = radeon_gfx9_read_reg(kGfx9Seg0, 0x0004);
	sGfx9Stall.grbm2 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0002);
	sGfx9Stall.cp = radeon_gfx9_read_reg(kGfx9Seg0, 0x01a0);
	sGfx9Stall.cpc = radeon_gfx9_read_reg(kGfx9Seg0, 0x0084);
	sGfx9Stall.cpInt = radeon_gfx9_read_reg(kGfx9Seg0, 0x104a);
	sGfx9Stall.vm = radeon_gfx9_read_reg(kGfx9Seg0, 0x084b);
	sGfx9Stall.vmLo = radeon_gfx9_read_reg(kGfx9Seg0, 0x084c);
	sGfx9Stall.vmHi = radeon_gfx9_read_reg(kGfx9Seg0, 0x084d);
	sGfx9Stall.safe = radeon_gfx9_read_reg(kGfx9Seg1, 0x4c05);
	sGfx9Stall.gfxCntl = radeon_gfx9_read_reg(kGfx9Seg0, 0x0022);
	sGfx9Stall.shMem = radeon_gfx9_read_reg(kGfx9Seg0, 0x030d);
	sGfx9Stall.cpfStatus = radeon_gfx9_read_reg(kGfx9Seg0, 0x0087);
	sGfx9Stall.cpfBusy = radeon_gfx9_read_reg(kGfx9Seg0, 0x0088);
	sGfx9Stall.cpfStalled = radeon_gfx9_read_reg(kGfx9Seg0, 0x0089);
	sGfx9Stall.stalled1 = radeon_gfx9_read_reg(kGfx9Seg0, 0x019d);
	sGfx9Stall.stalled2 = radeon_gfx9_read_reg(kGfx9Seg0, 0x019e);
	sGfx9Stall.busyStat = radeon_gfx9_read_reg(kGfx9Seg0, 0x019f);
	sGfx9Stall.pfpHeader = radeon_gfx9_read_reg(kGfx9Seg0, 0x01a2);
	sGfx9Stall.roqRbStat = radeon_gfx9_read_reg(kGfx9Seg0, 0x01e0);
	ERROR("%s: R5 radeon_hd: GFX9 stall grbm 0x%" B_PRIx32
		" grbm2 0x%" B_PRIx32 " cp 0x%" B_PRIx32 " cpc 0x%" B_PRIx32
		" int 0x%" B_PRIx32 " vm 0x%" B_PRIx32 " vmlo 0x%" B_PRIx32
		" vmhi 0x%" B_PRIx32 " safe 0x%" B_PRIx32 " gfxcntl 0x%"
		B_PRIx32 " shmem 0x%" B_PRIx32 "\n", __func__, sGfx9Stall.grbm,
		sGfx9Stall.grbm2, sGfx9Stall.cp, sGfx9Stall.cpc, sGfx9Stall.cpInt,
		sGfx9Stall.vm, sGfx9Stall.vmLo, sGfx9Stall.vmHi, sGfx9Stall.safe,
		sGfx9Stall.gfxCntl, sGfx9Stall.shMem);
	ERROR("%s: R5 radeon_hd: GFX9 stall cpf 0x%" B_PRIx32
		" cpfbusy 0x%" B_PRIx32 " cpfstall 0x%" B_PRIx32
		" stalled1 0x%" B_PRIx32 " stalled2 0x%" B_PRIx32
		" busystat 0x%" B_PRIx32 " pfp 0x%" B_PRIx32
		" roq 0x%" B_PRIx32 "\n", __func__, sGfx9Stall.cpfStatus,
		sGfx9Stall.cpfBusy, sGfx9Stall.cpfStalled, sGfx9Stall.stalled1,
		sGfx9Stall.stalled2, sGfx9Stall.busyStat, sGfx9Stall.pfpHeader,
		sGfx9Stall.roqRbStat);
}


static void
radeon_gfx9_stall_note()
{
	if (!sGfx9Stall.valid)
		return;
	r5_text_mode("a");
	r5_text("stall grbm %08x grbm2 %08x cp %08x cpc %08x int %08x"
		" vm %08x vmlo %08x vmhi %08x safe %08x gfxcntl %08x shmem %08x\n",
		sGfx9Stall.grbm, sGfx9Stall.grbm2, sGfx9Stall.cp, sGfx9Stall.cpc,
		sGfx9Stall.cpInt, sGfx9Stall.vm, sGfx9Stall.vmLo, sGfx9Stall.vmHi,
		sGfx9Stall.safe, sGfx9Stall.gfxCntl, sGfx9Stall.shMem);
	r5_text("stall cpf %08x cpfbusy %08x cpfstall %08x"
		" stalled1 %08x stalled2 %08x busystat %08x pfp %08x roq %08x\n",
		sGfx9Stall.cpfStatus, sGfx9Stall.cpfBusy, sGfx9Stall.cpfStalled,
		sGfx9Stall.stalled1, sGfx9Stall.stalled2, sGfx9Stall.busyStat,
		sGfx9Stall.pfpHeader, sGfx9Stall.roqRbStat);
}


// Linux v6.12 soc15_need_reset_on_init. The only APU in the Raven/Renoir
// series that returns true is CHIP_RENOIR. Raven and Picasso return false,
// and soc15_asic_reset returns before sending anything for those chips.
// amdgpu_drv.c lists 15e7, 1636, 1638 and 164c as CHIP_RENOIR. MP1 12.0.1
// then selects a mode-2 reset, which is smu_v12_0_mode2_reset: one SMU
// message, PPSMC_MSG_GfxDeviceDriverReset (0x1E), argument SMU_RESET_MODE_2
// (2). That is not a mode-1 PCI reset and it is not GRBM_SOFT_RESET.
// This driver sends it only from the GFX ring start. Lucienne 164c is in
// the set and is not reset here, because its firmware stays unloaded and
// DCN is not reprogrammed afterwards.
static bool
radeon_soc15_need_reset_on_init(uint32 pciID)
{
	return pciID == 0x15e7 || pciID == 0x1636 || pciID == 0x1638
		|| pciID == 0x164c;
}


// MP1_BASE.instance[0].segment[0] in vega10_ip_offset.h is 0x00016000.
// mmMP1_SMN_C2PMSG_66 is already 0x0282, so the 0x200 segment step is in
// that index. Adding 0x0282 to 0x00016200 reads dword 0x1649a, which stayed
// 0, and the ring never started. The response is dword 0x1629a. A previous
// response of 0 means the SMU is still in a command. PPSMC_Result_OK is 1.
// The wait is usec_timeout * 20, two seconds at the Linux default of 100 ms.
// A timeout does not send a second message. A response other than 1 does
// not start the command processor. The firmware scanout is not rewritten:
// this driver does not program DCN.
static bool
radeon_smu12_mode2_reset()
{
	const uint32 kMp1 = 0x00016000;
	const uint32 msg = (kMp1 + 0x0282) * 4;
	const uint32 arg = (kMp1 + 0x0292) * 4;
	const uint32 resp = (kMp1 + 0x029a) * 4;
	ERROR("%s: R5 radeon_hd: SMU mailbox byte 0x%" B_PRIx32 "\n",
		__func__, resp);
	sSoc15ResetSent = false;
	sSoc15ResetBefore = 0xffffffff;
	sSoc15ResetAfter = 0xffffffff;
	if (!radeon_mmio_fits(resp)) {
		ERROR("%s: R5 radeon_hd: SMU mailbox is outside mmio\n", __func__);
		return false;
	}

	// After the first command, idle means the previous response is
	// non-zero. Zero is still in progress. 100 ms matches the default
	// usec_timeout. The first command is different: smu_cmn treats
	// SMU_FW_INIT as "ignore the initial response", because power-on
	// leaves C2PMSG_90 at 0.
	static bool mailboxUsed = false;
	uint32 seen = 0;
	for (int i = 0; i < 100; i++) {
		seen = Read32(OUT, resp);
		if (seen != 0)
			break;
		snooze(1000);
	}
	sSoc15ResetBefore = seen;
	if (seen == 0 && mailboxUsed) {
		ERROR("%s: R5 radeon_hd: SMU mailbox stayed 0, mode 2 not sent\n",
			__func__);
		return false;
	}
	if (seen == 0)
		ERROR("%s: R5 radeon_hd: SMU mailbox initial 0, mode 2 sent\n",
			__func__);
	mailboxUsed = true;

	Write32(OUT, resp, 0);
	Write32(OUT, arg, 2);
	Write32(OUT, msg, 0x1e);
	sSoc15ResetSent = true;
	uint32 answer = 0;
	for (int i = 0; i < 2000; i++) {
		answer = Read32(OUT, resp);
		if (answer != 0)
			break;
		snooze(1000);
	}
	sSoc15ResetAfter = answer;
	ERROR("%s: R5 radeon_hd: SMU mode 2 before 0x%" B_PRIx32
		" after 0x%" B_PRIx32 "\n", __func__, seen, answer);
	if (answer != 1) {
		ERROR("%s: R5 radeon_hd: SMU mode 2 refused, ring left\n",
			__func__);
		return false;
	}
	return true;
}


static void
radeon_gfx9_contract_check(bool* ok, const char* name, uint32 value,
	uint32 mask, uint32 expected)
{
	if ((value & mask) == expected)
		return;
	*ok = false;
	ERROR("%s: R5 radeon_hd: GFX9 ring contract: %s 0x%" B_PRIx32
		" expected 0x%" B_PRIx32 " mask 0x%" B_PRIx32 "\n", __func__,
		name, value, expected, mask);
}


// The values are the ones this file writes. VM_L2_CNTL forces bits 0 and 1
// on, bit 8 and bit 18 off, bits 19-20 to 1, and bits 21-25 off. That is
// mask 0x03fc0103 and expected 0x00080003. A mismatch logs every line
// and then refuses the unhalt.
static bool
radeon_gfx9_verify_ring_config(uint32 cntl, uint64 ringGpu, uint32 location,
	uint32 top, bool ringDoorbell, bool mecStarted, bool pipePrepared,
	uint32 aperBefore)
{
	bool ok = true;
	radeon_gfx9_contract_check(&ok, "CP_RB0_CNTL",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_CNTL)), 0xffffffffu, cntl);
	uint64 base = ringGpu >> 8;
	radeon_gfx9_contract_check(&ok, "CP_RB0_BASE",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_BASE)), 0xffffffffu,
		(uint32)base);
	radeon_gfx9_contract_check(&ok, "CP_RB0_BASE_HI",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_BASE_HI)), 0xffffffffu,
		(uint32)(base >> 32));
	uint32 doorbellExpected = kGfx9DoorbellByte;
	if (ringDoorbell)
		doorbellExpected |= 1u << 30;
	radeon_gfx9_contract_check(&ok, "CP_RB_DOORBELL_CONTROL",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_DOORBELL_CONTROL)), 0x4ffffffcu,
		doorbellExpected);
	radeon_gfx9_contract_check(&ok, "CP_RB_DOORBELL_RANGE_LOWER",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_DOORBELL_RANGE_LOWER)), 0xffffffffu,
		kGfx9DoorbellByte);
	radeon_gfx9_contract_check(&ok, "CP_RB0_VMID",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_VMID)), 0xffffffffu, 0);
	radeon_gfx9_contract_check(&ok, "CP_ME0_PIPE0_MAX_CONTEXT",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_MAX_CONTEXT)), 0xffffffffu, 7);
	radeon_gfx9_contract_check(&ok, "CP_ME0_PIPE0_DEVICE_ID",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_DEVICE_ID)), 0xffffffffu, 1);
	if (pipePrepared) {
		radeon_gfx9_contract_check(&ok, "GRBM_GFX_CNTL",
			Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmGRBM_GFX_CNTL)), 0xffffffffu, 0);
		radeon_gfx9_contract_check(&ok, "SH_MEM_CONFIG",
			Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmSH_MEM_CONFIG)), 0xffffffffu,
			0x1018);
		radeon_gfx9_contract_check(&ok, "SH_MEM_BASES",
			Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmSH_MEM_BASES)), 0xffffffffu, 0);
	} else {
		ok = false;
		ERROR("%s: R5 radeon_hd: GFX9 ring contract: GRBM_GFX_CNTL"
			" not written\n", __func__);
	}
	if (mecStarted) {
		radeon_gfx9_contract_check(&ok, "CP_MEC_CNTL",
			Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_MEC_CNTL)), 0x50000000u, 0);
	}
	radeon_gfx9_contract_check(&ok, "RLC_CNTL",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmRLC_CNTL)), 0x1u, 1);
	radeon_gfx9_contract_check(&ok, "MC_VM_MX_L1_TLB_CNTL",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_MX_L1_TLB_CNTL)), 0x00003879u,
		0x00003859u);
	radeon_gfx9_contract_check(&ok, "VM_L2_CNTL",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL)), 0x03fc0103u,
		0x00080003u);
	radeon_gfx9_contract_check(&ok, "VM_L2_CNTL3",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_CNTL3)), 0xffffffffu,
		0x80130009u);
	radeon_gfx9_contract_check(&ok, "GCEA_PROBE_MAP",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmGCEA_PROBE_MAP)), 0xffffffffu,
		0x0000ccccu);
	if (aperBefore != 0xffffffff) {
		radeon_gfx9_contract_check(&ok, "RCC_DOORBELL_APER_EN",
			Read32(OUT, kNbioDoorbellAper), 0x1u, 1);
	}
	radeon_gfx9_contract_check(&ok, "MC_VM_FB_LOCATION_BASE",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_BASE)), 0xffffffffu,
		location);
	radeon_gfx9_contract_check(&ok, "MC_VM_FB_LOCATION_TOP",
		Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_TOP)), 0xffffffffu, top);
	if (ok)
		ERROR("%s: R5 radeon_hd: GFX9 ring contract verified\n", __func__);
	return ok;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The first fence has already landed. This submits one indirect buffer.
// The constants are the Linux v6.12 names in soc15d.h and vega10_enum.h.
static const uint32 kWlWriteData = 0x37;
static const uint32 kWlIndirectBuffer = 0x3f;
static const uint32 kWlReleaseMem = 0x49;
static const uint32 kWlWriteDstMemory = 5u << 8;
static const uint32 kWlWrConfirm = 1u << 20;
static const uint32 kWlIbValid = 1u << 23;
static const uint32 kWlCacheFlushTs = 0x14;
static const uint32 kWlDmaData = 0x50;
static const uint32 kWlDmaCpSync = 1u << 31;
static const uint32 kWlDmaSrcData = 2u << 29;
static const uint32 kWlDmaRawWait = 1u << 30;
static const uint32 kWlDataSelCounter = 3u << 29;
static const uint32 kFenceIbWord = 1024;
static const uint32 kFenceSequence = 1088;
static const uint32 kFenceTsStart = 1152;
static const uint32 kFenceTsEnd = 1216;
static const uint32 kDmaChunk = 0x100000;
static const uint32 kWlSubmitDwords = 12;
static_assert(kFenceIbWord < 4096, "GFX9 workload fence");
static_assert(kFenceSequence < 4096, "GFX9 workload fence");
static_assert(kFenceTsStart < 4096, "GFX9 workload fence");
static_assert(kFenceTsEnd < 4096, "GFX9 workload fence");
static_assert(kFenceSequence - kFenceIbWord == 64, "GFX9 workload fence");
static_assert(kFenceTsStart - kFenceSequence == 64, "GFX9 workload fence");
static_assert(kFenceTsEnd - kFenceTsStart == 64, "GFX9 workload fence");
static_assert(kWlSubmitDwords == 12, "GFX9 workload submission");


static void
radeon_gfx9_workload_note(uint32 writeback, uint32 sequence, uint64 seen,
	uint32 rptr, uint32 wptr)
{
	r5_text_mode("a");
	r5_text("ib_wb %08" B_PRIx32 "\n", writeback);
	r5_text("ib_seq %" B_PRIu32 " seen %" B_PRIu64 "\n", sequence,
		seen);
	r5_text("ib_rptr %08" B_PRIx32 " wptr %08" B_PRIx32 "\n", rptr,
		wptr);
}


static void
radeon_gfx9_workload_fail(const char* step, uint32 meCntl, uint32 haltMask,
	uint64 fenceGpuSeen, uint32 writeback)
{
	const uint32 rptrReg = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR);
	Write32(OUT, meCntl, haltMask);
	uint32 cpStat = radeon_gfx9_read_reg(kGfx9Seg0, 0x01a0);
	uint32 grbm = radeon_gfx9_read_reg(kGfx9Seg0, 0x0004);
	uint32 rptr = Read32(OUT, rptrReg);
	ERROR("%s: R5 radeon_hd: GFX9 workload failed: %s CP_STAT 0x%"
		B_PRIx32 " GRBM_STATUS 0x%" B_PRIx32 " rptr 0x%" B_PRIx32
		" fence 0x%" B_PRIx64 " wb 0x%" B_PRIx32 "\n", __func__, step,
		cpStat, grbm, rptr, fenceGpuSeen, writeback);
	radeon_gfx9_capture_stall();
}


static uint64 r5_ring_rptr();
static bool r5_ring_has_room(uint32 count);
static bool r5_ring_doorbell();


static bool
radeon_gfx9_submit(const uint32* packets, uint32 count, uint64 sequence,
	bool ringDoorbell, uint32 meCntl, uint32 haltMask)
{
	uint64 wptr = sRadeonGfx9Ring.submitted;
	uint32 dwords = sRadeonGfx9Ring.ringBytes / sizeof(uint32);
	if (sRadeonGfx9Ring.ringAddress == NULL || !r5_ring_has_room(count)) {
		radeon_gfx9_workload_fail("ring", meCntl, haltMask, 0, 0);
		return false;
	}

	uint32* ring = (uint32*)sRadeonGfx9Ring.ringAddress;
	for (uint32 i = 0; i < count; i++)
		ring[r5_gfx9_ring_index(wptr, i, dwords)] = packets[i];
	uint64 newWptr = wptr + count;
	volatile uint64* sequenceSlot = (volatile uint64*)
		(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
	*sequenceSlot = 0;
	radeon_gfx9_flush_visible(sRadeonGfx9Ring.ringAddress,
		sRadeonGfx9Ring.ringBytes);
	radeon_gfx9_flush_visible(sRadeonGfx9Ring.fenceAddress, 4096);
	radeon_visible_barrier();

	*(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + 512) = newWptr;
	radeon_gfx9_flush_visible(sRadeonGfx9Ring.fenceAddress, 4096);
	radeon_visible_barrier();
	if (ringDoorbell) {
		*(volatile uint64*)(sHost->doorbell + kGfx9DoorbellByte) = newWptr;
		radeon_visible_barrier();
	} else {
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_WPTR), (uint32)newWptr);
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_WPTR_HI),
			(uint32)(newWptr >> 32));
	}

	uint64 seen = 0;
	bool landed = false;
	for (int i = 0; i < 200; i++) {
		seen = *sequenceSlot;
		if (seen == sequence) {
			landed = true;
			break;
		}
		snooze(1000);
	}
	if (!landed) {
		radeon_gfx9_workload_fail("fence", meCntl, haltMask, seen, 0);
		return false;
	}
	sRadeonGfx9Ring.submitted = newWptr;
	return true;
}


static uint64 sUserSequence = 0;
/* How often a submit waited for room, and the highest used dword count.
 * Logging only. The room decision below is unchanged. */
static uint32 sRingWaits = 0;
static uint32 sRingMaxFill = 0;
/* Linux ring->current_ctx. 0 valid means the next job is a switch. */
static r5_cs_ring_ctx sRingCtx;
static int sGpuDebug = 0;
static int sFlushFence = 0;
static int sCondExec = 0;
static int sPasidMap = 0;
/* gfx9_vmid_per_file. The LUT write follows the flush for that file. */
static int sFilePasid = 0;
static int sCeMeta = 0;
static int sGdsSwitch = 0;
static struct r5_gds sGdsState;
static uint32 sJobPasid = 0;
static uint64 sJobFile = 0;
static uint32 sPasidOfVmid[16];
static uint64 sFileOfVmid[16];
static void (*sDeferStamp)(uint64 sequence) = 0;


bool
r5_gfx9_ring_ready()
{
	return sK0Ok && sRadeonGfx9Ring.ready
		&& sRadeonGfx9Ring.fenceAddress != NULL
		&& sRadeonGfx9Ring.ringAddress != NULL;
}


bool
r5_gfx9_user_room(uint32 dwords)
{
	return r5_gfx9_ring_ready() && r5_ring_has_room(dwords);
}


bool
r5_gfx9_fence_offset(uint64* offset)
{
	if (!r5_gfx9_ring_ready() || offset == NULL)
		return false;
	*offset = sRadeonGfx9Ring.fenceOffset;
	return true;
}


uint64
r5_gfx9_user_seen()
{
	if (sRadeonGfx9Ring.fenceAddress == NULL)
		return 0;
	return *(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + kR5UserFenceOffset);
}


uint64
r5_gfx9_user_fence_mc()
{
	if (!r5_gfx9_ring_ready())
		return 0;
	uint32 location = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_BASE));
	return radeon_gfx9_mc_addr(location,
		sRadeonGfx9Ring.fenceOffset + kR5UserFenceOffset);
}


uint64
r5_gfx9_k2_word_mc()
{
	if (!r5_gfx9_ring_ready())
		return 0;
	uint32 location = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_BASE));
	return radeon_gfx9_mc_addr(location,
		sRadeonGfx9Ring.fenceOffset + kFenceIbWord);
}


/* WRITE_DATA of 0xDEADBEEF to destVa, then three header-only NOPs.
 * PACKET3(PACKET3_NOP, 0x3FFF) is 0xFFFF1000. destVa is the address the
 * IB's VMID translates. A VMID 1 test passes its own page plus 64.
 * The upper half is the full 32 bits. The 0xffff mask is only IB_BASE_HI. */
bool
r5_gfx9_stage_ib_store(uint8* cpu, uint64 destVa)
{
	if (cpu == NULL || destVa == 0 || (destVa & 3ull) != 0)
		return false;
	uint32* ib = (uint32*)cpu;
	ib[0] = radeon_packet3(kWlWriteData, 3);
	ib[1] = kWlWriteDstMemory | kWlWrConfirm;
	ib[2] = (uint32)destVa;
	ib[3] = (uint32)(destVa >> 32);
	ib[4] = 0xDEADBEEF;
	ib[5] = 0xFFFF1000;
	ib[6] = 0xFFFF1000;
	ib[7] = 0xFFFF1000;
	return true;
}


/* gfx_v9_0_ring_test_ib, Linux 6.12. Five dwords, no padding NOPs.
 * The caller submits that length and reads the word without clflush. */
bool
r5_gfx9_stage_wb(uint8* cpu, uint64 destVa)
{
	if (cpu == NULL || destVa == 0 || (destVa & 3ull) != 0)
		return false;
	uint32* ib = (uint32*)cpu;
	memset(ib, 0, 32);
	ib[0] = radeon_packet3(kWlWriteData, 3);
	ib[1] = kWlWriteDstMemory | kWlWrConfirm;
	ib[2] = (uint32)destVa;
	ib[3] = (uint32)(destVa >> 32);
	ib[4] = 0xDEADBEEF;
	return true;
}


bool
r5_gfx9_stage_ib(uint8* cpu, uint64 destVa)
{
	if (!r5_gfx9_stage_ib_store(cpu, destVa))
		return false;
	radeon_gfx9_flush_visible(cpu, 64);
	radeon_visible_barrier();
	asm volatile("clflush %0" : "+m" (*(volatile char*)cpu) : : "memory");
	asm volatile("sfence" ::: "memory");
	return true;
}


void
r5_gfx9_cp_dump(const char* why, uint64 pte)
{
	if (sHost == NULL || sHost->log == NULL || why == NULL)
		return;
	uint32 rptr = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR));
	uint32 wptrLo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_WPTR));
	uint32 wptrHi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_WPTR_HI));
	uint64 fenceWptr = 0;
	uint64 bell = 0;
	if (sRadeonGfx9Ring.fenceAddress != NULL)
		fenceWptr = *(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + 512);
	if (sHost->doorbell != NULL
		&& sHost->doorbellBytes >= (uint64)kGfx9DoorbellByte + sizeof(uint64))
		bell = *(volatile uint64*)(sHost->doorbell + kGfx9DoorbellByte);
	uint32 cp = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_STAT));
	uint32 grbm = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmGRBM_STATUS));
	uint32 grbm2 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmGRBM_STATUS2));
	uint32 me = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_ME_CNTL));
	uint32 stall1 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_STALLED_STAT1));
	uint32 stall2 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_STALLED_STAT2));
	uint32 stall3 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_STALLED_STAT3));
	uint32 fault = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_PROTECTION_FAULT_STATUS));
	uint32 faultLo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_PROTECTION_FAULT_ADDR_LO32));
	uint32 faultHi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_PROTECTION_FAULT_ADDR_HI32));
	uint32 ibLo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB1_BASE_LO));
	uint32 ibHi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB1_BASE_HI));
	uint32 ibSz = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB1_BUFSZ));
	uint32 ibOff = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB1_OFFSET));
	uint32 words[8];
	memset(words, 0, sizeof(words));
	if (sRadeonGfx9Ring.ringAddress != NULL && sRadeonGfx9Ring.ringBytes >= 32) {
		uint32 dwords = sRadeonGfx9Ring.ringBytes / sizeof(uint32);
		uint32* ring = (uint32*)sRadeonGfx9Ring.ringAddress;
		uint32 slot = rptr & (dwords - 1u);
		for (uint32 i = 0; i < 8; i++)
			words[i] = ring[(slot + i) & (dwords - 1u)];
	}
	sHost->log(sHost->cookie,
		"radeon_hd: %s cp %08lx grbm %08lx grbm2 %08lx me %08lx seen %llu\n",
		why, (unsigned long)cp, (unsigned long)grbm, (unsigned long)grbm2,
		(unsigned long)me, (unsigned long long)r5_gfx9_user_seen());
	sHost->log(sHost->cookie,
		"radeon_hd: %s rptr %08lx wptr %08lx%08lx kern %llu fence %llu bell %llu\n",
		why, (unsigned long)rptr, (unsigned long)wptrHi, (unsigned long)wptrLo,
		(unsigned long long)sRadeonGfx9Ring.submitted,
		(unsigned long long)fenceWptr, (unsigned long long)bell);
	sHost->log(sHost->cookie,
		"radeon_hd: %s stall %08lx %08lx %08lx fault %08lx addr %08lx%08lx "
		"ib %08lx%08lx sz %08lx off %08lx pte %016llx\n",
		why, (unsigned long)stall1, (unsigned long)stall2, (unsigned long)stall3,
		(unsigned long)fault, (unsigned long)faultHi, (unsigned long)faultLo,
		(unsigned long)ibHi, (unsigned long)ibLo, (unsigned long)ibSz,
		(unsigned long)ibOff, (unsigned long long)pte);
	sHost->log(sHost->cookie,
		"radeon_hd: %s ring %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
		why, (unsigned long)words[0], (unsigned long)words[1],
		(unsigned long)words[2], (unsigned long)words[3],
		(unsigned long)words[4], (unsigned long)words[5],
		(unsigned long)words[6], (unsigned long)words[7]);
	uint32 defLo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32));
	uint32 defHi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32));
	uint32 faultCntl = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmVM_L2_PROTECTION_FAULT_CNTL));
	sHost->log(sHost->cookie,
		"radeon_hd: %s default %08lx%08lx cntl %08lx\n",
		why, (unsigned long)defHi, (unsigned long)defLo,
		(unsigned long)faultCntl);
	r5_gfx9_pipe_log(why);
}


/* gc_9_0_offset.h, BASE_IDX 0, segment 0. STALLED_STAT3/1/2 are
 * 0x019c/0x019d/0x019e. ME_HEADER_DUMP is 0x01a1, PFP_HEADER_DUMP
 * is 0x01a2, PFP_INSTR_PNTR is 0x01a5, ME_INSTR_PNTR is 0x01a6.
 * Eight header reads. A later note says one PFP header read does
 * not consume a packet; eight reads still show a FIFO if it is one.
 * MMIO only. */
void
r5_gfx9_pipe_log(const char* why)
{
	if (sHost == NULL || sHost->log == NULL || sHost->read32 == NULL
		|| why == NULL)
		return;
	uint32 stall3 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_STALLED_STAT3));
	uint32 stall1 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_STALLED_STAT1));
	uint32 stall2 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_STALLED_STAT2));
	uint32 pfpIp = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_PFP_INSTR_PNTR));
	uint32 meIp = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_ME_INSTR_PNTR));
	uint32 me[8];
	uint32 pfp[8];
	uint32 i;
	for (i = 0; i < 8; i++) {
		me[i] = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_ME_HEADER_DUMP));
		pfp[i] = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_PFP_HEADER_DUMP));
	}
	sHost->log(sHost->cookie,
		"radeon_hd: %s stall3 %08lx stall1 %08lx stall2 %08lx "
		"me_ip %08lx pfp_ip %08lx\n",
		why, (unsigned long)stall3, (unsigned long)stall1,
		(unsigned long)stall2, (unsigned long)meIp, (unsigned long)pfpIp);
	sHost->log(sHost->cookie,
		"radeon_hd: %s me hdr %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
		why, (unsigned long)me[0], (unsigned long)me[1],
		(unsigned long)me[2], (unsigned long)me[3],
		(unsigned long)me[4], (unsigned long)me[5],
		(unsigned long)me[6], (unsigned long)me[7]);
	sHost->log(sHost->cookie,
		"radeon_hd: %s pfp hdr %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
		why, (unsigned long)pfp[0], (unsigned long)pfp[1],
		(unsigned long)pfp[2], (unsigned long)pfp[3],
		(unsigned long)pfp[4], (unsigned long)pfp[5],
		(unsigned long)pfp[6], (unsigned long)pfp[7]);
}


/* gc_9_0_offset.h mmCP_ROQ_IB1_STAT, segment 0 index 0x01e1. MMIO only. */
uint32
r5_gfx9_roq_ib1()
{
	if (sHost == NULL || sHost->read32 == NULL)
		return 0;
	return Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_ROQ_IB1_STAT));
}


void
r5_gfx9_stage_bytes(uint8* cpu, uint32 bytes)
{
	if (cpu == NULL || bytes == 0)
		return;
	radeon_gfx9_flush_visible(cpu, bytes);
	radeon_visible_barrier();
	for (uint32 i = 0; i < bytes; i += 64) {
		asm volatile("clflush %0" : "+m" (*(volatile char*)(cpu + i))
			: : "memory");
	}
	asm volatile("sfence" ::: "memory");
}


/* gc_9_0_offset.h, segment 1. BASE_LO/HI/BUFSZ and OFFSET for both IBs.
 * MMIO only. Safe from the interrupt handler. */
void
r5_gfx9_ib_regs(r5_gfx9_ib_now* ib1, r5_gfx9_ib_now* ib2)
{
	if (ib1 == NULL || ib2 == NULL)
		return;
	uint32 lo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB1_BASE_LO));
	uint32 hi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB1_BASE_HI));
	ib1->base = r5_cs_ib_va(lo, hi);
	ib1->size = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB1_BUFSZ));
	ib1->offset = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB1_OFFSET));
	lo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB2_BASE_LO));
	hi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB2_BASE_HI));
	ib2->base = r5_cs_ib_va(lo, hi);
	ib2->size = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB2_BUFSZ));
	ib2->offset = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, mmCP_IB2_OFFSET));
}


/* One user job: two SWITCH_BUFFER packets, CONTEXT_CONTROL,
 * FRAME_CONTROL, the indirect buffers, FRAME_CONTROL end, the 64-bit
 * user fence, the 32-bit interrupt fence, SWITCH_BUFFER, then the
 * Linux NOP pad to a multiple of 256 dwords. CONTEXT_CONTROL follows
 * gfx_v9_ring_emit_cntxcntl: the load bits are set when this file and
 * AMDGPU_CTX differ from the previous job on the ring. The K ring is
 * not padded. The fence addresses are kernel MC addresses. A full
 * ring returns false. */
static uint64 sTlbSeq = 0;
static uint64 sTlbFlushed = 0;
static uint64 sTlbPd = 0;


void
r5_gfx9_vmid1_set_root(uint64 pd)
{
	sTlbPd = pd;
}


void
r5_gfx9_vmid1_touch()
{
	sTlbSeq++;
	if (sTlbSeq == 0)
		sTlbSeq = 1;
}


bool
r5_gfx9_vmid1_flush_due(uint64* pd)
{
	if (sTlbPd == 0 || sTlbSeq == 0 || sTlbSeq == sTlbFlushed)
		return false;
	if (pd != NULL)
		*pd = sTlbPd;
	return true;
}


void
r5_gfx9_vmid1_flushed()
{
	sTlbFlushed = sTlbSeq;
}


uint32
r5_gfx9_user_commit_dwords(uint32 count)
{
	uint64 pd = 0;
	uint32 body;
	uint32 extra;
	if (!r5_gfx9_ring_ready())
		return 0;
	body = r5_cs_user_words(count);
	if (body == 0)
		return 0;
	extra = r5_gfx9_vmid1_flush_due(&pd)
		? r5_cs_vm_switch_words() : 0;
	if (extra != 0 && sJobPasid != 0 && (sPasidMap || sFilePasid))
		extra += 5u;
	return extra + body + r5_cs_pad_dwords(
		sRadeonGfx9Ring.submitted + extra + body);
}


void
r5_gfx9_set_debug(int on)
{
	sGpuDebug = on ? 1 : 0;
}


void
r5_gfx9_set_flush_fence(int on)
{
	sFlushFence = on ? 1 : 0;
}


void
r5_gfx9_set_cond_exec(int on)
{
	sCondExec = on ? 1 : 0;
}


void
r5_gfx9_set_pasid_map(int on)
{
	sPasidMap = on ? 1 : 0;
}


void
r5_gfx9_set_file_pasid(int on)
{
	sFilePasid = on ? 1 : 0;
}


void
r5_gfx9_set_ce_meta(int on)
{
	sCeMeta = on ? 1 : 0;
}


int
r5_gfx9_ce_meta_on(void)
{
	return sCeMeta;
}


int
r5_gfx9_flush_fence_on(void)
{
	return sFlushFence;
}


int
r5_gfx9_cond_exec_on(void)
{
	return sCondExec;
}


int
r5_gfx9_gds_switch_on(void)
{
	return sGdsSwitch;
}


int
r5_gfx9_pasid_map_on(void)
{
	return sPasidMap;
}


void
r5_gfx9_set_gds_switch(int on)
{
	sGdsSwitch = on ? 1 : 0;
}


void
r5_gfx9_set_job_pasid(uint32 pasid, uint64 file)
{
	sJobPasid = pasid;
	sJobFile = file;
}


void
r5_gfx9_set_defer_stamp(void (*fn)(uint64 sequence))
{
	sDeferStamp = fn;
}


uint64
r5_gfx9_file_for_vmid(uint32 vmid)
{
	if (vmid > 15u)
		return 0;
	return sFileOfVmid[vmid];
}


uint32
r5_gfx9_pasid_for_vmid(uint32 vmid)
{
	if (vmid > 15u)
		return 0;
	return sPasidOfVmid[vmid];
}


bool
r5_gfx9_user_submit(const r5_gfx9_user_ib* ibs, uint32 count,
	uint64 fenceCtx, int32* preambleSeen, uint64* _sequence)
{
	if (ibs == NULL || count == 0 || count > 8)
		return false;
	int haveSwitch = r5_cs_ctx_switch(&sRingCtx, fenceCtx);
	uint32 preamble = 0;
	for (uint32 i = 0; i < count; i++) {
		if ((ibs[i].flags & kR5IbPreamble) != 0)
			preamble |= kR5PreamblePresent;
	}
	if ((preamble & kR5PreamblePresent) != 0
		&& (preambleSeen == NULL || *preambleSeen == 0))
		preamble |= kR5PreambleFirst;
	uint64 ibVa[8];
	uint32 ibDw[8];
	uint32 ibVm[8];
	uint32 emit = 0;
	for (uint32 i = 0; i < count; i++) {
		if (r5_cs_skip_preamble_ib(ibs[i].flags, haveSwitch, preamble))
			continue;
		ibVa[emit] = ibs[i].va;
		ibDw[emit] = ibs[i].dwords;
		ibVm[emit] = ibs[i].vmid;
		emit++;
	}
	count = emit;
	if (count == 0)
		return false;
	uint64 pd = 0;
	bool flush = r5_gfx9_vmid1_flush_due(&pd);
	/* Sequence compares stay on the job number, which is the last
	 * fence. The flush fence is the number before it. Compared with
	 * >= or > in gpu_wait, the retire scan, the lost-client park, and
	 * sHighestSequence. gpu_stage_pace_wait uses == on the job number,
	 * so the earlier flush value does not finish that wait. */
	int flushFence = sFlushFence && flush;
	/* v6.18 amdgpu_vm_flush returns before any packet unless a VM
	 * flush, a GDS switch, a pipeline sync or the cleaner shader is
	 * needed. A PASID change alone writes nothing. Inside the block
	 * the mapping is written. The block then ends with one fence, when
	 * gfx9_flush_fence is on and a flush or a mapping happened, and
	 * the two SWITCH_BUFFER packets. */
	struct r5_gds jobGds;
	memset(&jobGds, 0, sizeof(jobGds));
	int gds = sGdsSwitch && r5_gds_switch_needed(&sGdsState, &jobGds);
	int inBlock = flush || gds;
	uint32 mapVmid = 1;
	if (flush && count > 0 && ibVm[0] >= 1u && ibVm[0] <= 15u)
		mapVmid = ibVm[0];
	int pasidMap = inBlock
		&& r5_pasid_map_due(sPasidMap || sFilePasid,
			sPasidOfVmid[mapVmid], sJobPasid);
	int endFence = sFlushFence && (flush || pasidMap);
	/* The CS ioctl has no GDS chunk. A GFX9 RADV job then carries
	 * zeros, and the packet runs once for this VMID. */
	uint32 prefix = flush ? r5_cs_vm_switch_words() : 0;
	if (flushFence)
		prefix += sCondExec ? 8u : 12u;
	else if (endFence)
		prefix += 8u;
	/* v6.18 amdgpu_vm_flush emits one COND_EXEC before the pipe sync
	 * and patches it before the double SWITCH_BUFFER. v6.12 does the
	 * same. v6.18 also counts the cleaner shader in that early return.
	 * amdgpu_ib_schedule emits the second COND_EXEC before
	 * CONTEXT_CONTROL and patches it after the job fence, so the last
	 * SWITCH_BUFFER stays outside. v6.18 emits that one when job is
	 * set; v6.12 emitted it when vm was set. */
	int condFlush = sCondExec && (flush || gds);
	if (condFlush)
		prefix += 5u;
	if (sCondExec)
		prefix += 4u + 5u;
	if (gds)
		prefix += 20u;
	if (pasidMap)
		prefix += 5u;
	uint64 wptr = sRadeonGfx9Ring.submitted;
	uint32 body = r5_cs_user_words(count);
	if (flushFence || sCondExec)
		body -= 4u;
	if (sCondExec)
		body -= 2u;
	if (sCeMeta)
		body += 14u;
	uint32 tail = sCondExec ? 2u : 0u;
	uint32 total = body == 0 ? 0 : prefix + body + tail
		+ r5_cs_pad_dwords(wptr + prefix + body + tail);
	uint32 cap = flush ? 512u : 320u;
	if (gds)
		cap += 20u;
	if (pasidMap)
		cap += 5u;
	if (endFence && !flushFence)
		cap += 8u;
	if (sCondExec)
		cap += 10u + 4u;
	if (sCeMeta)
		cap += 14u;
	if (total == 0 || total > cap || !r5_gfx9_user_room(total))
		return false;
	if (flush && (pd == 0 || ibVm[0] == 0))
		return false;
	uint64 fenceMc = r5_gfx9_user_fence_mc();
	if (fenceMc == 0 || (fenceMc & 7ull) != 0)
		return false;
	uint64 irqMc = fenceMc + (kR5UserIrqOffset - kR5UserFenceOffset);
	uint64 flushSeq = sUserSequence + 1;
	uint64 sequence = endFence ? flushSeq + 1 : flushSeq;
	uint32 cntx = r5_cs_cntx(haveSwitch, preamble);
	uint32 packets[576];
	uint32 switched = 0;
	uint64 pageMc = fenceMc - kR5UserFenceOffset;
	uint32 slotA = 0;
	if (condFlush) {
		slotA = switched + 4u;
		switched = r5_cs_cond_exec(packets, cap, switched,
			r5_cs_cond_exec_mc(pageMc));
	}
	if (flush) {
		switched = r5_cs_vm_switch(packets, cap, ibVm[0], pd,
			fenceMc, sUserSequence);
		if (sJobPasid != 0 && ibVm[0] < 16u) {
			sPasidOfVmid[ibVm[0]] = sJobPasid;
			sFileOfVmid[ibVm[0]] = sJobFile;
		}
	}
	if (pasidMap) {
		switched = r5_cs_pasid_map(packets, cap, switched, mapVmid,
			sJobPasid);
		sPasidOfVmid[mapVmid] = sJobPasid;
		sFileOfVmid[mapVmid] = sJobFile;
	}
	if (gds) {
		switched = r5_cs_gds_switch(packets, cap, switched, 1, &jobGds);
		sGdsState = jobGds;
		sGdsState.valid = 1;
	}
	if (flush && flushFence) {
		if (sCondExec)
			switched = r5_cs_eop_fence(packets, cap, switched, irqMc,
				flushSeq);
		else
			switched = r5_cs_flush_fence(packets, cap, switched, irqMc,
				flushSeq);
	} else if (endFence) {
		switched = r5_cs_eop_fence(packets, cap, switched, irqMc,
			flushSeq);
	}
	if (condFlush)
		r5_cs_cond_exec_patch(packets, slotA, switched - (slotA + 1u));
	uint32 slotB = 0;
	if (sCondExec) {
		switched = r5_cs_put(packets, switched, cap, r5_cs_packet3(0x8b, 0));
		switched = r5_cs_put(packets, switched, cap, 0);
		switched = r5_cs_put(packets, switched, cap, r5_cs_packet3(0x8b, 0));
		switched = r5_cs_put(packets, switched, cap, 0);
		slotB = switched + 4u;
		switched = r5_cs_cond_exec(packets, cap, switched,
			r5_cs_cond_exec_mc(pageMc));
	}
	if (flush && switched != prefix)
		return false;
	if (!flush && switched != prefix)
		return false;
	uint32 filled = r5_cs_user_body(packets + switched, cap - switched, ibVa,
		ibDw, ibVm, count, fenceMc, sequence, irqMc, cntx,
		(flushFence || sCondExec) ? 1u : 0u, sCondExec ? 1u : 0u,
		sCeMeta ? 1u : 0u);
	if (filled == 0)
		return false;
	if (sCondExec) {
		r5_cs_cond_exec_patch(packets, slotB, filled);
		filled += switched;
		packets[filled] = r5_cs_packet3(0x8b, 0);
		packets[filled + 1u] = 0;
		filled += 2u;
	} else
		filled += switched;
	uint32 pad = r5_cs_pad_dwords(wptr + filled);
	if (filled + pad != total)
		return false;
	r5_cs_pad(packets + filled, pad);
	filled += pad;
	uint32 dwords = sRadeonGfx9Ring.ringBytes / sizeof(uint32);
	uint32* ring = (uint32*)sRadeonGfx9Ring.ringAddress;
	for (uint32 i = 0; i < filled; i++)
		ring[r5_gfx9_ring_index(wptr, i, dwords)] = packets[i];
	uint64 newWptr = wptr + filled;
	radeon_gfx9_flush_visible(sRadeonGfx9Ring.ringAddress,
		sRadeonGfx9Ring.ringBytes);
	radeon_visible_barrier();
	*(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + 512) = newWptr;
	radeon_gfx9_flush_visible(sRadeonGfx9Ring.fenceAddress, 4096);
	radeon_visible_barrier();
	if (r5_ring_doorbell()) {
		*(volatile uint64*)(sHost->doorbell + kGfx9DoorbellByte) = newWptr;
		radeon_visible_barrier();
	} else {
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_WPTR), (uint32)newWptr);
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_WPTR_HI),
			(uint32)(newWptr >> 32));
	}
	sRadeonGfx9Ring.submitted = newWptr;
	sUserSequence = sequence;
	r5_cs_ctx_note(&sRingCtx, fenceCtx);
	if ((preamble & kR5PreamblePresent) != 0 && preambleSeen != NULL)
		*preambleSeen = 1;
	if (flush) {
		r5_gfx9_vmid1_flushed();
		if (sDeferStamp != NULL)
			sDeferStamp(flushFence ? flushSeq : sequence);
	}
	if (sGpuDebug && sHost != NULL && sHost->log != NULL) {
		uint32 show[8];
		memset(show, 0, sizeof(show));
		uint32 nShow = filled < 8u ? filled : 8u;
		for (uint32 i = 0; i < nShow; i++)
			show[i] = packets[i];
		sHost->log(sHost->cookie,
			"radeon_hd: t %lld user job fence %llx flush %lu "
			"cntx %08lx seq %llu %08lx %08lx %08lx %08lx %08lx %08lx "
			"%08lx %08lx\n",
			(long long)system_time(), (unsigned long long)fenceCtx,
			(unsigned long)(flush ? 1u : 0u),
			(unsigned long)cntx, (unsigned long long)sequence,
			(unsigned long)show[0], (unsigned long)show[1],
			(unsigned long)show[2], (unsigned long)show[3],
			(unsigned long)show[4], (unsigned long)show[5],
			(unsigned long)show[6], (unsigned long)show[7]);
	}
	if (_sequence != NULL)
		*_sequence = sequence;
	return true;
}


static uint32
radeon_gfx9_mismatch(volatile uint32* words, uint32 count, uint32 expect,
	uint32* firstOffset)
{
	uint32 bad = 0;
	for (uint32 i = 0; i < count; i++) {
		if (words[i] == expect)
			continue;
		if (bad == 0 && firstOffset != NULL)
			*firstOffset = i * 4;
		bad++;
	}
	return bad;
}


static void
radeon_gfx9_dma_note(uint32 bad1, uint64 ticks1, uint32 bad8, uint64 ticks8,
	bool have8)
{
	r5_text_mode("a");
	r5_text("dma_1m_bad %" B_PRIu32 " ticks_1m %" B_PRIu64 "\n",
		bad1, ticks1);
	if (have8) {
		uint32 whole = 0;
		uint32 tenth = 0;
		if (ticks1 != 0) {
			whole = (uint32)(ticks8 / ticks1);
			tenth = (uint32)(((ticks8 % ticks1) * 10) / ticks1);
		}
		r5_text("dma_8m_bad %" B_PRIu32 " ticks_8m %" B_PRIu64 "\n",
			bad8, ticks8);
		if (ticks1 == 0)
			r5_text("dma_ratio na\n");
		else
			r5_text("dma_ratio %" B_PRIu32 ".%" B_PRIu32 "\n",
				whole, tenth);
	}
}


static uint32
radeon_gfx9_dma_ib(uint32* ib, uint64 fenceGpu, uint64 dstGpu, uint32 chunks,
	uint32 pattern = kK2Pattern)
{
	const uint32 eop = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| kWlCacheFlushTs | (5u << 8);
	uint32 n = 0;
	uint64 startGpu = fenceGpu + kFenceTsStart;
	ib[n++] = radeon_packet3(kWlReleaseMem, 6);
	ib[n++] = eop;
	ib[n++] = kWlDataSelCounter;
	ib[n++] = (uint32)startGpu;
	ib[n++] = (uint32)(startGpu >> 32);
	ib[n++] = 0;
	ib[n++] = 0;
	ib[n++] = 0;
	for (uint32 chunk = 0; chunk < chunks; chunk++) {
		uint64 dest = dstGpu + (uint64)chunk * kDmaChunk;
		ib[n++] = radeon_packet3(kWlDmaData, 5);
		ib[n++] = kWlDmaCpSync | kWlDmaSrcData;
		ib[n++] = pattern;
		ib[n++] = 0;
		ib[n++] = (uint32)dest;
		ib[n++] = (uint32)(dest >> 32);
		ib[n++] = kWlDmaRawWait | kDmaChunk;
	}
	uint64 endGpu = fenceGpu + kFenceTsEnd;
	ib[n++] = radeon_packet3(kWlReleaseMem, 6);
	ib[n++] = eop;
	ib[n++] = kWlDataSelCounter;
	ib[n++] = (uint32)endGpu;
	ib[n++] = (uint32)(endGpu >> 32);
	ib[n++] = 0;
	ib[n++] = 0;
	ib[n++] = 0;
	return n;
}


static void
radeon_gfx9_dma(uint32 location, uint64 fenceGpu, bool ringDoorbell,
	uint32 meCntl, uint32 haltMask, uint8* ibAddress, uint64 ibGpu,
	uint64 firstSequence = 2)
{
	uint64 targetOffset = 0;
	uint8* target = NULL;
	const uint32 bytes8 = 8u * 1024u * 1024u;
	const uint32 bytes1 = 1024u * 1024u;
	const uint32 dwords1 = bytes1 / sizeof(uint32);
	if (r5_vram(bytes8, 4096, &targetOffset, &target) != B_OK
		|| target == NULL) {
		ERROR("%s: R5 radeon_hd: GFX9 workload failed: dma alloc\n",
			__func__);
		return;
	}
	memset(target, 0, bytes8);
	radeon_gfx9_flush_visible(target, bytes8);
	radeon_visible_barrier();
	uint64 dstGpu = radeon_gfx9_mc_addr(location, targetOffset);
	uint64 sequenceGpu = fenceGpu + kFenceSequence;
	volatile uint64* tsStart = (volatile uint64*)
		(sRadeonGfx9Ring.fenceAddress + kFenceTsStart);
	volatile uint64* tsEnd = (volatile uint64*)
		(sRadeonGfx9Ring.fenceAddress + kFenceTsEnd);

	uint32* ib = (uint32*)ibAddress;
	uint32 ibDwords = radeon_gfx9_dma_ib(ib, fenceGpu, dstGpu, 1);
	radeon_gfx9_flush_visible(ibAddress, 4096);
	radeon_visible_barrier();
	*tsStart = 0;
	*tsEnd = 0;
	uint32 packets[kWlSubmitDwords];
	uint64 sequence = firstSequence;
	packets[0] = radeon_packet3(kWlIndirectBuffer, 2);
	packets[1] = (uint32)ibGpu & ~3u;
	packets[2] = (uint32)((ibGpu >> 32) & 0xffffu);
	packets[3] = kWlIbValid | ibDwords;
	packets[4] = radeon_packet3(kWlReleaseMem, 6);
	packets[5] = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| kWlCacheFlushTs | (5u << 8);
	packets[6] = 1u << 29;
	packets[7] = (uint32)sequenceGpu;
	packets[8] = (uint32)(sequenceGpu >> 32);
	packets[9] = (uint32)sequence;
	packets[10] = (uint32)(sequence >> 32);
	packets[11] = 0;
	if (!radeon_gfx9_submit(packets, kWlSubmitDwords, sequence, ringDoorbell, meCntl,
			haltMask))
		return;

	uint32 off1 = 0;
	uint32 bad1 = radeon_gfx9_mismatch((volatile uint32*)target, dwords1,
		0xA5A5A5A5, &off1);
	uint32 tail = radeon_gfx9_mismatch(
		(volatile uint32*)(target + bytes1), dwords1, 0, NULL);
	uint64 ticks1 = *tsEnd - *tsStart;
	if (bad1 != 0 || tail != 0) {
		radeon_gfx9_dma_note(bad1, ticks1, 0, 0, false);
		ERROR("%s: R5 radeon_hd: GFX9 workload dma 1m bad %" B_PRIu32
			" off 0x%" B_PRIx32 " tail %" B_PRIu32 " ticks %" B_PRIu64
			"\n", __func__, bad1, off1, tail, ticks1);
		radeon_gfx9_workload_fail("dma_1m", meCntl, haltMask, sequence,
			bad1);
		return;
	}

	ibDwords = radeon_gfx9_dma_ib(ib, fenceGpu, dstGpu, 8);
	radeon_gfx9_flush_visible(ibAddress, 4096);
	radeon_visible_barrier();
	*tsStart = 0;
	*tsEnd = 0;
	sequence = firstSequence + 1;
	packets[3] = kWlIbValid | ibDwords;
	packets[9] = (uint32)sequence;
	packets[10] = (uint32)(sequence >> 32);
	if (!radeon_gfx9_submit(packets, kWlSubmitDwords, sequence, ringDoorbell, meCntl,
			haltMask))
		return;

	uint32 off8 = 0;
	uint32 bad8 = radeon_gfx9_mismatch((volatile uint32*)target,
		bytes8 / sizeof(uint32), 0xA5A5A5A5, &off8);
	uint64 ticks8 = *tsEnd - *tsStart;
	radeon_gfx9_dma_note(bad1, ticks1, bad8, ticks8, true);
	if (bad8 != 0 || ticks1 == 0 || ticks8 <= ticks1) {
		ERROR("%s: R5 radeon_hd: GFX9 workload dma 8m bad %" B_PRIu32
			" off 0x%" B_PRIx32 " ticks %" B_PRIu64 " ticks1 %" B_PRIu64
			"\n", __func__, bad8, off8, ticks8, ticks1);
		radeon_gfx9_workload_fail("dma_8m", meCntl, haltMask, sequence,
			bad8);
		return;
	}
	uint32 whole = (uint32)(ticks8 / ticks1);
	uint32 tenth = (uint32)(((ticks8 % ticks1) * 10) / ticks1);
	ERROR("%s: R5 radeon_hd: GFX9 workload dma 1m bad 0 ticks %" B_PRIu64
		" 8m bad 0 ticks %" B_PRIu64 " ratio %" B_PRIu32 ".%" B_PRIu32
		"\n", __func__, ticks1, ticks8, whole, tenth);
}


static void
radeon_gfx9_workload(uint32 location, uint64 ringGpu, uint64 fenceGpu,
	bool ringDoorbell, uint32 meCntl, uint32 haltMask)
{
	(void)ringGpu;
	uint64 ibOffset = 0;
	uint8* ibAddress = NULL;
	if (r5_vram(4096, 4096, &ibOffset, &ibAddress) != B_OK
		|| ibAddress == NULL) {
		ERROR("%s: R5 radeon_hd: GFX9 workload failed: alloc\n", __func__);
		return;
	}
	uint64 ibGpu = radeon_gfx9_mc_addr(location, ibOffset);
	uint64 wordGpu = fenceGpu + kFenceIbWord;
	uint64 sequenceGpu = fenceGpu + kFenceSequence;
	uint32* ib = (uint32*)ibAddress;
	ib[0] = radeon_packet3(kWlWriteData, 3);
	ib[1] = kWlWriteDstMemory | kWlWrConfirm;
	ib[2] = (uint32)wordGpu;
	ib[3] = (uint32)(wordGpu >> 32);
	ib[4] = 0xDEADBEEF;
	*(volatile uint32*)(sRadeonGfx9Ring.fenceAddress + kFenceIbWord)
		= 0xCAFEDEAD;
	radeon_gfx9_flush_visible(ibAddress, 4096);
	radeon_visible_barrier();

	uint64 sequence = 1;
	uint32 packets[kWlSubmitDwords];
	packets[0] = radeon_packet3(kWlIndirectBuffer, 2);
	packets[1] = (uint32)ibGpu & ~3u;
	packets[2] = (uint32)((ibGpu >> 32) & 0xffffu);
	packets[3] = kWlIbValid | 5u;
	packets[4] = radeon_packet3(kWlReleaseMem, 6);
	packets[5] = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| kWlCacheFlushTs | (5u << 8);
	packets[6] = (1u << 29);
	packets[7] = (uint32)sequenceGpu;
	packets[8] = (uint32)(sequenceGpu >> 32);
	packets[9] = (uint32)sequence;
	packets[10] = (uint32)(sequence >> 32);
	packets[11] = 0;

	if (!radeon_gfx9_submit(packets, kWlSubmitDwords, sequence, ringDoorbell, meCntl,
			haltMask)) {
		uint32 writeback = *(volatile uint32*)
			(sRadeonGfx9Ring.fenceAddress + kFenceIbWord);
		uint64 seen = *(volatile uint64*)
			(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
		uint32 rptr = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR));
		uint64 published = *(volatile uint64*)
			(sRadeonGfx9Ring.fenceAddress + 512);
		radeon_gfx9_workload_note(writeback, (uint32)sequence, seen, rptr,
			(uint32)published);
		return;
	}

	uint32 writeback = *(volatile uint32*)
		(sRadeonGfx9Ring.fenceAddress + kFenceIbWord);
	uint64 seen = *(volatile uint64*)
		(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
	uint32 rptr = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR));
	uint32 wptr = sRadeonGfx9Ring.submitted;
	radeon_gfx9_workload_note(writeback, (uint32)sequence, seen, rptr, wptr);
	if (writeback != 0xDEADBEEF || seen != sequence || rptr != wptr) {
		sRadeonGfx9Ring.submitted = wptr - 12;
		radeon_gfx9_workload_fail("check", meCntl, haltMask, seen,
			writeback);
		return;
	}
	ERROR("%s: R5 radeon_hd: GFX9 workload ib wb 0x%" B_PRIx32
		" seq %" B_PRIu64 " seen %" B_PRIx64 " rptr 0x%" B_PRIx32
		" wptr 0x%" B_PRIx32 "\n", __func__, writeback, sequence, seen,
		rptr, wptr);
	radeon_gfx9_dma(location, fenceGpu, ringDoorbell, meCntl, haltMask,
		ibAddress, ibGpu);
}




static void
radeon_gfx9_compute_note(const char* line)
{
	r5_text_mode("a");
	r5_text("%s\n", line);
}


static void
radeon_gfx9_compute_stop(const char* stage, uint32 meCntl, uint32 haltMask,
	uint64 sequence, uint64 seen)
{
	Write32(OUT, meCntl, haltMask);
	uint32 cpStat = radeon_gfx9_read_reg(kGfx9Seg0, 0x01a0);
	uint32 grbm = radeon_gfx9_read_reg(kGfx9Seg0, 0x0004);
	uint32 grbm2 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0002);
	uint32 grbmSe0 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0005);
	uint32 stalled1 = radeon_gfx9_read_reg(kGfx9Seg0, 0x019d);
	uint32 stalled2 = radeon_gfx9_read_reg(kGfx9Seg0, 0x019e);
	uint32 cpf = radeon_gfx9_read_reg(kGfx9Seg0, 0x0087);
	uint32 rptr = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR));
	uint32 roq = radeon_gfx9_read_reg(kGfx9Seg0, 0x01e0);
	uint32 readError = radeon_gfx9_read_reg(kGfx9Seg0, 0x0016);
	uint32 readError2 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0017);
	uint32 vm = radeon_gfx9_read_reg(kGfx9Seg0, 0x084b);
	uint32 vmLo = radeon_gfx9_read_reg(kGfx9Seg0, 0x084c);
	uint32 vmHi = radeon_gfx9_read_reg(kGfx9Seg0, 0x084d);
	uint32 pgmLo = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e0c);
	uint32 pgmHi = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e0d);
	uint32 rsrc1 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e12);
	uint32 rsrc2 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e13);
	uint32 threads = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e07);
	uint32 user0 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e40);
	uint32 user1 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e41);
	uint32 se0 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e16);
	uint32 initiator = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e00);
	ERROR("%s: R5 radeon_hd: GFX9 compute failed: %s seq %" B_PRIu64
		" seen %" B_PRIu64 " CP_STAT 0x%" B_PRIx32 " GRBM_STATUS 0x%"
		B_PRIx32 " GRBM_STATUS2 0x%" B_PRIx32 " GRBM_STATUS_SE0 0x%"
		B_PRIx32 "\n", __func__, stage, sequence, seen, cpStat, grbm,
		grbm2, grbmSe0);
	ERROR("%s: R5 radeon_hd: GFX9 compute failed: stalled 0x%" B_PRIx32
		" 0x%" B_PRIx32 " cpf 0x%" B_PRIx32 " rptr 0x%" B_PRIx32
		" roq 0x%" B_PRIx32 " readerr 0x%" B_PRIx32 " 0x%" B_PRIx32
		" vm 0x%" B_PRIx32 " 0x%" B_PRIx32 " 0x%" B_PRIx32 "\n",
		__func__, stalled1, stalled2, cpf, rptr, roq, readError,
		readError2, vm, vmLo, vmHi);
	ERROR("%s: R5 radeon_hd: GFX9 compute regs pgm 0x%" B_PRIx32
		" 0x%" B_PRIx32 " rsrc 0x%" B_PRIx32 " 0x%" B_PRIx32
		" threads %" B_PRIu32 " user 0x%" B_PRIx32 " 0x%" B_PRIx32
		" se0 0x%" B_PRIx32 " init 0x%" B_PRIx32 "\n", __func__,
		pgmLo, pgmHi, rsrc1, rsrc2, threads, user0, user1, se0,
		initiator);
	radeon_gfx9_capture_stall();
}


static bool
radeon_gfx9_compute_shader(uint32 location, const uint32* code, uint32 bytes,
	uint64* _mc)
{
	uint64 offset = 0;
	uint8* address = NULL;
	if (r5_vram(4096, kGfx9ShaderAlign, &offset, &address) != B_OK
		|| address == NULL)
		return false;
	memset(address, 0, 4096);
	memcpy(address, code, bytes);
	radeon_gfx9_flush_visible(address, 4096);
	radeon_visible_barrier();
	*_mc = radeon_gfx9_mc_addr(location, offset);
	return (*_mc & 255ull) == 0;
}


static bool
radeon_gfx9_compute_buffer(uint32 location, uint32 bytes, uint64* _mc,
	uint8** _cpu)
{
	uint64 offset = 0;
	uint8* address = NULL;
	if (r5_vram(bytes, 4096, &offset, &address) != B_OK
		|| address == NULL)
		return false;
	uint32* words = (uint32*)address;
	uint32 count = bytes / sizeof(uint32);
	for (uint32 i = 0; i < count; i++)
		words[i] = 0xCAFEDEAD;
	radeon_gfx9_flush_visible(address, bytes);
	radeon_visible_barrier();
	*_mc = radeon_gfx9_mc_addr(location, offset);
	*_cpu = address;
	return true;
}


static uint32
radeon_gfx9_compute_check(volatile uint32* words, uint32 dwords,
	uint32 threads, uint32 samples[][3], uint32 sampleCount, bool* stillDead)
{
	uint32 bad = 0;
	*stillDead = false;
	for (uint32 i = 0; i < dwords; i++) {
		uint32 want = i < threads ? (i * 3u + 7u) : 0xCAFEDEAD;
		uint32 got = words[i];
		if (got == want)
			continue;
		if (bad < sampleCount) {
			samples[bad][0] = i;
			samples[bad][1] = got;
			samples[bad][2] = want;
		}
		if (got == 0xCAFEDEAD)
			*stillDead = true;
		bad++;
	}
	return bad;
}


static bool
radeon_gfx9_compute_dispatch(uint8* ibAddress, uint64 ibGpu, uint64 fenceGpu,
	uint64 shaderMc, uint32 rsrc1, uint32 rsrc2, uint64 bufferMc,
	uint32 groups, uint64 sequence, bool ringDoorbell, uint32 meCntl,
	uint32 haltMask, const char* stage)
{
	uint32* ib = (uint32*)ibAddress;
	uint32 ibDwords = r5_gfx9_ib_compute(ib, shaderMc, rsrc1, rsrc2, bufferMc,
		groups, fenceGpu + kFenceTsStart, fenceGpu + kFenceTsEnd);
	if (ibDwords != kGfx9ComputeIbDwords) {
		ERROR("%s: R5 radeon_hd: GFX9 compute failed: %s ib %" B_PRIu32
			"\n", __func__, stage, ibDwords);
		return false;
	}
	radeon_gfx9_flush_visible(ibAddress, 4096);
	radeon_visible_barrier();
	volatile uint64* tsStart = (volatile uint64*)
		(sRadeonGfx9Ring.fenceAddress + kFenceTsStart);
	volatile uint64* tsEnd = (volatile uint64*)
		(sRadeonGfx9Ring.fenceAddress + kFenceTsEnd);
	*tsStart = 0;
	*tsEnd = 0;
	uint32 packets[12];
	if (r5_gfx9_submit(packets, ibGpu, ibDwords, fenceGpu + kFenceSequence,
			sequence) != 12) {
		ERROR("%s: R5 radeon_hd: GFX9 compute failed: %s submit\n",
			__func__, stage);
		return false;
	}
	if (!radeon_gfx9_submit(packets, 12, sequence, ringDoorbell, meCntl,
			haltMask)) {
		uint64 seen = *(volatile uint64*)
			(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
		char line[80];
		snprintf(line, sizeof(line),
			"%s fail seq %" B_PRIu64 " seen %" B_PRIu64,
			stage, sequence, seen);
		radeon_gfx9_compute_note(line);
		radeon_gfx9_compute_stop(stage, meCntl, haltMask, sequence, seen);
		return false;
	}
	return true;
}


static bool
radeon_gfx9_compute(uint32 location, uint64 fenceGpu, bool ringDoorbell,
	uint32 meCntl, uint32 haltMask)
{
	uint64 ibOffset = 0;
	uint8* ibAddress = NULL;
	if (r5_vram(4096, 4096, &ibOffset, &ibAddress) != B_OK
		|| ibAddress == NULL) {
		ERROR("%s: R5 radeon_hd: GFX9 compute failed: alloc\n", __func__);
		return false;
	}
	uint64 ibGpu = radeon_gfx9_mc_addr(location, ibOffset);
	uint64 wordGpu = fenceGpu + kFenceIbWord;
	uint64 sequenceGpu = fenceGpu + kFenceSequence;
	uint32* ib = (uint32*)ibAddress;
	ib[0] = radeon_packet3(kWlWriteData, 3);
	ib[1] = kWlWriteDstMemory | kWlWrConfirm;
	ib[2] = (uint32)wordGpu;
	ib[3] = (uint32)(wordGpu >> 32);
	ib[4] = 0xDEADBEEF;
	*(volatile uint32*)(sRadeonGfx9Ring.fenceAddress + kFenceIbWord)
		= 0xCAFEDEAD;
	radeon_gfx9_flush_visible(ibAddress, 4096);
	radeon_visible_barrier();

	uint64 sequence = 1;
	uint32 packets[kWlSubmitDwords];
	packets[0] = radeon_packet3(kWlIndirectBuffer, 2);
	packets[1] = (uint32)ibGpu & ~3u;
	packets[2] = (uint32)((ibGpu >> 32) & 0xffffu);
	packets[3] = kWlIbValid | 5u;
	packets[4] = radeon_packet3(kWlReleaseMem, 6);
	packets[5] = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| kWlCacheFlushTs | (5u << 8);
	packets[6] = (1u << 29);
	packets[7] = (uint32)sequenceGpu;
	packets[8] = (uint32)(sequenceGpu >> 32);
	packets[9] = (uint32)sequence;
	packets[10] = (uint32)(sequence >> 32);
	packets[11] = 0;
	if (!radeon_gfx9_submit(packets, kWlSubmitDwords, sequence, ringDoorbell,
			meCntl, haltMask)) {
		uint32 writeback = *(volatile uint32*)
			(sRadeonGfx9Ring.fenceAddress + kFenceIbWord);
		uint64 seen = *(volatile uint64*)
			(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
		uint32 rptr = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR));
		uint64 published = *(volatile uint64*)
			(sRadeonGfx9Ring.fenceAddress + 512);
		radeon_gfx9_workload_note(writeback, (uint32)sequence, seen, rptr,
			(uint32)published);
		return false;
	}
	uint32 writeback = *(volatile uint32*)
		(sRadeonGfx9Ring.fenceAddress + kFenceIbWord);
	uint64 seen = *(volatile uint64*)
		(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
	uint32 rptr = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR));
	uint32 wptr = sRadeonGfx9Ring.submitted;
	radeon_gfx9_workload_note(writeback, (uint32)sequence, seen, rptr, wptr);
	if (writeback != 0xDEADBEEF || seen != sequence || rptr != wptr) {
		sRadeonGfx9Ring.submitted = wptr - 12;
		radeon_gfx9_workload_fail("check", meCntl, haltMask, seen, writeback);
		return false;
	}
	ERROR("%s: R5 radeon_hd: GFX9 workload ib wb 0x%" B_PRIx32
		" seq %" B_PRIu64 " seen %" B_PRIx64 " rptr 0x%" B_PRIx32
		" wptr 0x%" B_PRIx32 "\n", __func__, writeback, sequence, seen,
		rptr, wptr);

	uint64 emptyMc = 0;
	uint64 fillMc = 0;
	if (!radeon_gfx9_compute_shader(location, kGfx9EmptyCode,
			sizeof(kGfx9EmptyCode), &emptyMc)
		|| !radeon_gfx9_compute_shader(location, kGfx9FillCode,
			sizeof(kGfx9FillCode), &fillMc)) {
		ERROR("%s: R5 radeon_hd: GFX9 compute failed: shader alloc\n",
			__func__);
		return false;
	}
	uint64 buf4k = 0;
	uint64 buf4m = 0;
	uint64 buf16m = 0;
	uint8* cpu4k = NULL;
	uint8* cpu4m = NULL;
	uint8* cpu16m = NULL;
	const uint32 bytes4k = 4096;
	const uint32 bytes4m = 4u * 1024u * 1024u;
	const uint32 bytes16m = 16u * 1024u * 1024u;
	if (!radeon_gfx9_compute_buffer(location, bytes4k, &buf4k, &cpu4k)
		|| !radeon_gfx9_compute_buffer(location, bytes4m, &buf4m, &cpu4m)
		|| !radeon_gfx9_compute_buffer(location, bytes16m, &buf16m, &cpu16m)) {
		ERROR("%s: R5 radeon_hd: GFX9 compute failed: buffer alloc\n",
			__func__);
		return false;
	}

	sequence = 2;
	if (!radeon_gfx9_compute_dispatch(ibAddress, ibGpu, fenceGpu, emptyMc,
			kGfx9EmptyRsrc1, kGfx9EmptyRsrc2, buf4k, 1, sequence,
			ringDoorbell, meCntl, haltMask, "compute_c0"))
		return false;
	seen = *(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
	char line[160];
	snprintf(line, sizeof(line),
		"compute_c0 ok seq %" B_PRIu64 " seen %" B_PRIu64, sequence, seen);
	radeon_gfx9_compute_note(line);
	ERROR("%s: R5 radeon_hd: GFX9 %s\n", __func__, line);

	sequence = 3;
	if (!radeon_gfx9_compute_dispatch(ibAddress, ibGpu, fenceGpu, fillMc,
			kGfx9FillRsrc1, kGfx9FillRsrc2, buf4k, 1, sequence,
			ringDoorbell, meCntl, haltMask, "compute_c1"))
		return false;
	uint32 samples[8][3];
	bool stillDead = false;
	uint32 bad = radeon_gfx9_compute_check((volatile uint32*)cpu4k,
		bytes4k / sizeof(uint32), 64, samples, 8, &stillDead);
	uint32 pgmLo = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e0c);
	uint32 pgmHi = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e0d);
	uint32 rsrc1 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e12);
	uint32 rsrc2 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e13);
	uint32 threads = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e07);
	uint32 user0 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e40);
	uint32 user1 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e41);
	uint32 se0 = radeon_gfx9_read_reg(kGfx9Seg0, 0x0e16);
	snprintf(line, sizeof(line),
		"compute_regs pgm %08" B_PRIx32 " %08" B_PRIx32 " rsrc %08"
		B_PRIx32 " %08" B_PRIx32 " threads %" B_PRIu32 " user %08"
		B_PRIx32 " %08" B_PRIx32 " se0 %08" B_PRIx32, pgmLo, pgmHi,
		rsrc1, rsrc2, threads, user0, user1, se0);
	radeon_gfx9_compute_note(line);
	ERROR("%s: R5 radeon_hd: GFX9 %s\n", __func__, line);
	if (bad != 0) {
		snprintf(line, sizeof(line),
			"compute_c1 fail bad %" B_PRIu32 " first %" B_PRIu32
			" got %08" B_PRIx32 " want %08" B_PRIx32,
			bad, samples[0][0], samples[0][1], samples[0][2]);
		radeon_gfx9_compute_note(line);
		seen = *(volatile uint64*)
			(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
		ERROR("%s: R5 radeon_hd: GFX9 %s dead %s\n", __func__, line,
			stillDead ? "yes" : "no");
		for (uint32 i = 0; i < 8 && i < bad; i++) {
			ERROR("%s: R5 radeon_hd: GFX9 compute c1 wrong %" B_PRIu32
				" got 0x%" B_PRIx32 " want 0x%" B_PRIx32 "\n",
				__func__, samples[i][0], samples[i][1], samples[i][2]);
		}
		radeon_gfx9_compute_stop("compute_c1", meCntl, haltMask, sequence, seen);
		return false;
	}
	snprintf(line, sizeof(line),
		"compute_c1 ok bad 0 first 0 got 00000000 want 00000000");
	radeon_gfx9_compute_note(line);
	ERROR("%s: R5 radeon_hd: GFX9 %s\n", __func__, line);

	struct {
		const char*	tag;
		uint64		sequence;
		uint32		groups;
		uint32		bytes;
		uint64		bufferMc;
		uint8*		cpu;
	} fill[2];
	fill[0].tag = "compute_c2";
	fill[0].sequence = 4;
	fill[0].groups = 16384;
	fill[0].bytes = bytes4m;
	fill[0].bufferMc = buf4m;
	fill[0].cpu = cpu4m;
	fill[1].tag = "compute_c3";
	fill[1].sequence = 5;
	fill[1].groups = 65536;
	fill[1].bytes = bytes16m;
	fill[1].bufferMc = buf16m;
	fill[1].cpu = cpu16m;
	uint64 ticks[2];
	for (uint32 stage = 0; stage < 2; stage++) {
		if (!radeon_gfx9_compute_dispatch(ibAddress, ibGpu, fenceGpu, fillMc,
				kGfx9FillRsrc1, kGfx9FillRsrc2, fill[stage].bufferMc,
				fill[stage].groups, fill[stage].sequence, ringDoorbell,
				meCntl, haltMask, fill[stage].tag))
			return false;
		stillDead = false;
		bad = radeon_gfx9_compute_check((volatile uint32*)fill[stage].cpu,
			fill[stage].bytes / sizeof(uint32), fill[stage].groups * 64u,
			samples, 8, &stillDead);
		volatile uint64* tsStart = (volatile uint64*)
			(sRadeonGfx9Ring.fenceAddress + kFenceTsStart);
		volatile uint64* tsEnd = (volatile uint64*)
			(sRadeonGfx9Ring.fenceAddress + kFenceTsEnd);
		ticks[stage] = *tsEnd - *tsStart;
		if (bad != 0) {
			snprintf(line, sizeof(line),
				"%s fail bad %" B_PRIu32 " ticks %" B_PRIu64,
				fill[stage].tag, bad, ticks[stage]);
			radeon_gfx9_compute_note(line);
			seen = *(volatile uint64*)
				(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
			ERROR("%s: R5 radeon_hd: GFX9 %s first %" B_PRIu32
				" got 0x%" B_PRIx32 " want 0x%" B_PRIx32 " dead %s\n",
				__func__, line, samples[0][0], samples[0][1],
				samples[0][2], stillDead ? "yes" : "no");
			for (uint32 i = 0; i < 8 && i < bad; i++) {
				ERROR("%s: R5 radeon_hd: GFX9 compute %s wrong %"
					B_PRIu32 " got 0x%" B_PRIx32 " want 0x%" B_PRIx32
					"\n", __func__, fill[stage].tag, samples[i][0],
					samples[i][1], samples[i][2]);
			}
			radeon_gfx9_compute_stop(fill[stage].tag, meCntl, haltMask,
				fill[stage].sequence, seen);
			return false;
		}
		snprintf(line, sizeof(line),
			"%s ok bad 0 ticks %" B_PRIu64, fill[stage].tag,
			ticks[stage]);
		radeon_gfx9_compute_note(line);
		ERROR("%s: R5 radeon_hd: GFX9 %s\n", __func__, line);
	}
	if (ticks[0] == 0) {
		radeon_gfx9_compute_note("compute_ratio na");
		ERROR("%s: R5 radeon_hd: GFX9 compute_ratio na\n", __func__);
	} else {
		uint32 whole = (uint32)(ticks[1] / ticks[0]);
		uint32 tenth = (uint32)(((ticks[1] % ticks[0]) * 10) / ticks[0]);
		snprintf(line, sizeof(line), "compute_ratio %" B_PRIu32 ".%"
			B_PRIu32, whole, tenth);
		radeon_gfx9_compute_note(line);
		ERROR("%s: R5 radeon_hd: GFX9 %s\n", __func__, line);
	}
	if (sGfx9Workload) {
		radeon_gfx9_dma(location, fenceGpu, ringDoorbell, meCntl, haltMask,
			ibAddress, ibGpu, 6);
	}
	return true;
}
// SPDX-SnippetEnd


static void
radeon_gfx9_cp_start()
{
	if (!sRadeonGfx9Ring.ready)
		return;

	// A previous PSP load must not keep SRM on for a later direct load.
	sRlcRestoreLists = false;
	sRadeonGfx9Saved.armed = false;
	if (!radeon_gfx9_save_hw()) {
		ERROR("%s: R5 radeon_hd: GFX9 register snapshot refused\n",
			__func__);
		return;
	}

	uint32 cntl = radeon_gfx9_rb_cntl(sRadeonGfx9Ring.ringBytes);
	if (cntl == 0 || sRadeonGfx9Ring.submitted == 0
		|| sRadeonGfx9Ring.submitted
			>= sRadeonGfx9Ring.ringBytes / sizeof(uint32)) {
		ERROR("%s: R5 radeon_hd: GFX9 ring size refused\n", __func__);
		return;
	}

	uint32 highest = radeon_gfx9_byte(kGfx9Seg1, mmCP_CE_UCODE_DATA);
	if (!radeon_mmio_fits(highest)
		|| !radeon_mmio_fits(radeon_gfx9_byte(kGfx9Seg1, mmSCRATCH_REG0))) {
		ERROR("%s: R5 radeon_hd: GFX9 mmio does not cover CP ucode\n",
			__func__);
		return;
	}

	// Before any CP or aperture write. A refused reset leaves the ring
	// unprogrammed. The firmware picture is not rewritten.
	if (radeon_soc15_need_reset_on_init(sPciId)
		&& !radeon_smu12_mode2_reset()) {
		if (sSoc15ResetSent)
			radeon_gfx9_restore_saved(0, false, 0);
		radeon_gfx9_publish(false, false, 0, 0, 0, 0, false, false, 0, 0, 0);
		return;
	}

	const uint32 meCntl = radeon_gfx9_byte(kGfx9Seg0, mmCP_ME_CNTL);
	const uint32 fbOffset = radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_OFFSET);
	const uint32 rbCntl = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_CNTL);
	const uint32 rbRptrAddr = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR_ADDR);
	const uint32 rbRptrAddrHi = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR_ADDR_HI);
	const uint32 rbPollLo = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_WPTR_POLL_ADDR_LO);
	const uint32 rbPollHi = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_WPTR_POLL_ADDR_HI);
	const uint32 deviceId = radeon_gfx9_byte(kGfx9Seg0, mmCP_DEVICE_ID);
	const uint32 rbVmid = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_VMID);
	const uint32 rbWptr = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_WPTR);
	const uint32 rbWptrHi = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_WPTR_HI);
	const uint32 rbDoorbell = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_DOORBELL_CONTROL);
	const uint32 rbRangeLo = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_DOORBELL_RANGE_LOWER);
	const uint32 rbRangeHi = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_DOORBELL_RANGE_UPPER);
	const uint32 maxContext = radeon_gfx9_byte(kGfx9Seg0, mmCP_MAX_CONTEXT);
	const uint32 rbBase = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_BASE);
	const uint32 rbBaseHi = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_BASE_HI);
	const uint32 rbRptr = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR);
	const uint32 rbRptrWr = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_RPTR_WR);
	const uint32 rbDelay = radeon_gfx9_byte(kGfx9Seg0, mmCP_RB_WPTR_DELAY);
	const uint32 scratch = radeon_gfx9_byte(kGfx9Seg1, mmSCRATCH_REG0);
	const uint32 haltMask = radeon_gfx9_halt_mask();

	uint32 savedMe = Read32(OUT, meCntl);
	ERROR("%s: R5 radeon_hd: GFX9 CP_ME_CNTL 0x%" B_PRIx32
		" before load\n", __func__, savedMe);

	Write32(OUT, meCntl, savedMe | haltMask);
	snooze(1000);

	uint32 location = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_BASE));
	uint32 top = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_TOP));
	uint32 fb = Read32(OUT, fbOffset);
	uint64 ringGpu = radeon_gfx9_mc_addr(location, sRadeonGfx9Ring.ringOffset);
	uint64 ringPa = radeon_gfx9_pa_addr(fb, sRadeonGfx9Ring.ringOffset);
	sGfx9FbLocation = location;
	sGfx9FbTop = top;
	sGfx9FbOffset = fb;
	sGfx9RingMc = ringGpu;
	sGfx9RingPa = ringPa;
	ERROR("%s: R5 radeon_hd: GFX9 FB_LOCATION 0x%" B_PRIx32
		"-0x%" B_PRIx32 " FB_OFFSET 0x%" B_PRIx32
		" ring mc 0x%" B_PRIx64 " pa 0x%" B_PRIx64 "\n",
		__func__, location, top, fb, ringGpu, ringPa);
	uint32 loc = location & MC_VM_FB_LOCATION_BASE__FB_BASE_MASK;
	uint32 tp = top & MC_VM_FB_LOCATION_BASE__FB_BASE_MASK;
	bool window = location != 0xffffffff && top != 0xffffffff
		&& fb != 0xffffffff && fb != 0 && loc <= tp
		&& sVramBytes
			<= (((uint64)tp + 1ull) << 24) - ((uint64)loc << 24)
		&& (ringGpu & 0xff) == 0;
	if (!window) {
		Write32(OUT, meCntl, savedMe);
		radeon_gfx9_restore_saved(0, false, 0);
		ERROR("%s: R5 radeon_hd: GFX9 ring base refused\n", __func__);
		return;
	}
	if (!radeon_gfx9_prepare_hw(location, fb)) {
		Write32(OUT, meCntl, savedMe);
		radeon_gfx9_restore_saved(0, false, 0);
		ERROR("%s: R5 radeon_hd: GFX9 aperture refused\n", __func__);
		return;
	}

	bool pspOwnsLoad = false;
	if (radeon_mmio_fits(radeon_psp_byte(0x91))
		&& radeon_mmio_fits(radeon_psp_byte(0x87))) {
		uint32 sol = Read32(OUT, radeon_psp_byte(0x91));
		uint32 boot = Read32(OUT, radeon_psp_byte(0x63));
		ERROR("%s: R5 radeon_hd: PSP C2PMSG_81 0x%" B_PRIx32
			" C2PMSG_35 0x%" B_PRIx32 "\n", __func__, sol, boot);
		pspOwnsLoad = sol != 0 && sol != 0xffffffff;
	}

	// The firmware RLC is still running after mode 2. Halt it before the
	// PSP accepts a new image. A later failure writes this pair back.
	bool rlcTouched = false;
	uint32 rlcBios = 0;
	uint32 rlcClockBios = 0;
	if (pspOwnsLoad)
		rlcTouched = radeon_gfx9_rlc_halt(&rlcBios, &rlcClockBios);

	status_t loaded = B_ERROR;
	if (pspOwnsLoad)
		loaded = radeon_psp_load_engines(location, fb);
	else {
		char pfp[32];
		char ce[32];
		char me[32];
		const char* stem = radeon_gfx9_fw_stem();
		if (stem == NULL)
			loaded = B_ERROR;
		else {
			snprintf(pfp, sizeof(pfp), "%s_pfp.bin", stem);
			snprintf(ce, sizeof(ce), "%s_ce.bin", stem);
			snprintf(me, sizeof(me), "%s_me.bin", stem);
			loaded = radeon_gfx9_load_engine(pfp, 0x5814, 0x5815, 0);
			if (loaded == B_OK)
				loaded = radeon_gfx9_load_engine(ce, 0x5818, 0x5819, 0);
			if (loaded == B_OK)
				loaded = radeon_gfx9_load_engine(me, 0x5816, 0x5817, 0);
		}
	}
	if (loaded != B_OK) {
		Write32(OUT, meCntl, savedMe);
		if (rlcTouched)
			radeon_gfx9_restore_saved(rlcBios, true, rlcClockBios);
		else
			radeon_gfx9_restore_saved(0, false, 0);
		radeon_gfx9_publish(pspOwnsLoad, false, rlcBios, 0, 0, 0, false,
			false, 0, 0, 0);
		ERROR("%s: R5 radeon_hd: GFX9 firmware not running, ring left\n",
			__func__);
		return;
	}

	bool rlcStarted = false;
	uint32 rlcBefore = 0;
	uint32 rlcClock = 0;
	uint32 rlcAfter = 0;
	uint32 rlcVersion = 0;
	// PSP already loaded RLC. The direct path loads it while F32 is stopped,
	// after the clear-state buffer, matching gfx_v9_0_rlc_resume.
	rlcStarted = radeon_gfx9_rlc_start(location, !pspOwnsLoad, &rlcBefore,
		&rlcClock, &rlcAfter, &rlcVersion);
	if (!rlcStarted) {
		Write32(OUT, meCntl, savedMe);
		if (rlcTouched)
			radeon_gfx9_restore_saved(rlcBios, true, rlcClockBios);
		else
			radeon_gfx9_restore_saved(0, false, 0);
		radeon_gfx9_publish(pspOwnsLoad, false, rlcBefore, rlcAfter,
			rlcVersion, 0, false, false, 0, 0, 0);
		return;
	}

	// gfx_v9_0_cp_compute_enable(adev, true) runs in gfx_v9_0_kiq_resume,
	// before gfx_v9_0_cp_gfx_resume. No KIQ queue and no MQD here: the
	// MEC only runs idle. mmCP_MEC_CNTL is 0x008d, base index 0.
	// MEC_ME1_HALT is bit 30 and MEC_ME2_HALT is bit 28.
	uint32 savedMec = 0;
	bool mecStarted = false;
	const uint32 mecCntl = radeon_gfx9_byte(kGfx9Seg0, mmCP_MEC_CNTL);
	if (!sGfx9MecLoaded) {
		ERROR("%s: R5 radeon_hd: MEC not started, not loaded\n", __func__);
	} else {
		savedMec = Read32(OUT, mecCntl);
		Write32(OUT, mecCntl, 0);
		snooze(50);
		ERROR("%s: R5 radeon_hd: GFX9 CP_MEC_CNTL 0x%" B_PRIx32 " -> 0x%"
			B_PRIx32 "\n", __func__, savedMec, Read32(OUT, mecCntl));
		mecStarted = true;
	}

	Write32(OUT, scratch, 0xCAFEDEAD);
	uint32 scratchRead = Read32(OUT, scratch);
	if (scratchRead != 0xCAFEDEAD) {
		Write32(OUT, meCntl, savedMe);
		if (mecStarted)
			Write32(OUT, mecCntl, savedMec);
		radeon_gfx9_restore_saved(rlcTouched ? rlcBios : rlcBefore,
			rlcTouched || rlcStarted,
			rlcTouched ? rlcClockBios : rlcClock);
		radeon_gfx9_publish(pspOwnsLoad, rlcStarted, rlcBefore, rlcAfter,
			rlcVersion, scratchRead, false, false, 0, 0, 0);
		ERROR("%s: R5 radeon_hd: GFX9 scratch read 0x%" B_PRIx32
			", ring left\n", __func__, scratchRead);
		return;
	}

	uint32 savedCntl = Read32(OUT, rbCntl);
	uint32 savedBase = Read32(OUT, rbBase);
	uint32 savedBaseHi = Read32(OUT, rbBaseHi);
	uint32 savedWptr = Read32(OUT, rbWptr);
	uint32 savedWptrHi = Read32(OUT, rbWptrHi);
	uint32 savedDoorbell = Read32(OUT, rbDoorbell);
	uint32 savedRangeLo = Read32(OUT, rbRangeLo);
	uint32 savedRangeHi = Read32(OUT, rbRangeHi);
	uint32 savedContext = Read32(OUT, maxContext);
	uint32 savedDevice = Read32(OUT, deviceId);
	uint32 savedDelay = Read32(OUT, rbDelay);
	uint32 savedVmid = Read32(OUT, rbVmid);
	uint32 savedRptrAddr = Read32(OUT, rbRptrAddr);
	uint32 savedRptrAddrHi = Read32(OUT, rbRptrAddrHi);
	uint32 savedPollLo = Read32(OUT, rbPollLo);
	uint32 savedPollHi = Read32(OUT, rbPollHi);

	uint64 fenceGpu = ringGpu - sRadeonGfx9Ring.ringOffset
		+ sRadeonGfx9Ring.fenceOffset;
	uint64 rptrGpu = fenceGpu + 256;
	uint64 pollGpu = fenceGpu + 512;
	*(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + 256) = 0;
	*(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + 512) = 0;
	radeon_gfx9_flush_visible(sRadeonGfx9Ring.ringAddress,
		sRadeonGfx9Ring.ringBytes);
	radeon_gfx9_flush_visible(sRadeonGfx9Ring.fenceAddress, 4096);
	radeon_visible_barrier();

	Write32(OUT, rbDelay, 0);
	Write32(OUT, rbVmid, 0);
	Write32(OUT, rbCntl, cntl);
	Write32(OUT, rbWptr, 0);
	Write32(OUT, rbWptrHi, 0);
	Write32(OUT, rbRptrAddr, (uint32)rptrGpu);
	Write32(OUT, rbRptrAddrHi, (uint32)((rptrGpu >> 32) & 0xffff));
	Write32(OUT, rbPollLo, (uint32)pollGpu);
	Write32(OUT, rbPollHi, (uint32)((pollGpu >> 32) & 0xffff));
	snooze(1000);
	Write32(OUT, rbCntl, cntl);
	uint64 base = ringGpu >> 8;
	Write32(OUT, rbBase, (uint32)base);
	Write32(OUT, rbBaseHi, (uint32)(base >> 32));
	uint32 rptrBefore = Read32(OUT, rbRptr);
	Write32(OUT, rbCntl, cntl | (1u << 31));
	Write32(OUT, rbRptrWr, 0);
	Write32(OUT, rbCntl, cntl);
	uint32 rptrZero = Read32(OUT, rbRptr);
	uint32 aperBefore = 0xffffffff;
	uint32 aperAfter = 0xffffffff;
	uint32 sdma0Range = 0xffffffff;
	uint32 sdma1Range = 0xffffffff;
	uint32 ihRange = 0xffffffff;
	uint32 mmschRange = 0xffffffff;
	// soc15_common_hw_init enables RCC_DOORBELL_APER_EN before the CP
	// ring test. nbio_v7_0_init_registers is empty. The SDMA, IH and
	// VCN ranges are only read here: a write inside those ranges is not
	// routed to the CP, and their current values are not known yet.
	if (radeon_mmio_fits(kNbioMmschRange)) {
		aperBefore = Read32(OUT, kNbioDoorbellAper);
		sdma0Range = Read32(OUT, kNbioSdma0Range);
		sdma1Range = Read32(OUT, kNbioSdma1Range);
		ihRange = Read32(OUT, kNbioIhRange);
		mmschRange = Read32(OUT, kNbioMmschRange);
		if (aperBefore != 0xffffffff) {
			Write32(OUT, kNbioDoorbellAper, aperBefore | 1u);
			aperAfter = Read32(OUT, kNbioDoorbellAper);
		}
	}
	ERROR("%s: R5 radeon_hd: NBIO aperture 0x%" B_PRIx32
		" -> 0x%" B_PRIx32 " sdma0 0x%" B_PRIx32 " sdma1 0x%" B_PRIx32
		" ih 0x%" B_PRIx32 " mmsch 0x%" B_PRIx32 "\n", __func__,
		aperBefore, aperAfter, sdma0Range, sdma1Range, ihRange,
		mmschRange);
	bool ringDoorbell = sHost->doorbell != NULL
		&& sHost->doorbellBytes >= (uint64)kGfx9DoorbellByte + sizeof(uint64);
	uint32 doorbell = Read32(OUT, rbDoorbell);
	doorbell &= ~0x0ffffffcu;
	doorbell &= ~(1u << 30);
	doorbell |= kGfx9DoorbellByte;
	if (ringDoorbell)
		doorbell |= 1u << 30;
	Write32(OUT, rbDoorbell, doorbell);
	// gfx_v9_0_cp_gfx_resume writes RANGE_LOWER from the GFX doorbell
	// index whether or not the enable bit is set. Upper is the field mask.
	Write32(OUT, rbRangeLo, kGfx9DoorbellByte);
	Write32(OUT, rbRangeHi, 0x0ffffffc);
	Write32(OUT, maxContext, 7);
	Write32(OUT, deviceId, 1);
	// gfx_v9_0_constants_init does this before the first PM4 packet.
	// GRBM_GFX_CNTL 0 selects ME 0, pipe 0, queue 0 and VMID 0. GC 9.3
	// sets noretry, so RETRY_DISABLE is bit 12. GFX9 ALIGNMENT_MODE
	// unaligned is 3, shifted by 3.
	const uint32 grbmGfx = radeon_gfx9_byte(kGfx9Seg0, mmGRBM_GFX_CNTL);
	const uint32 shMem = radeon_gfx9_byte(kGfx9Seg0, mmSH_MEM_CONFIG);
	const uint32 shBases = radeon_gfx9_byte(kGfx9Seg0, mmSH_MEM_BASES);
	uint32 savedGrbmGfx = 0;
	uint32 savedShMem = 0;
	uint32 savedShBases = 0;
	bool pipePrepared = false;
	if (radeon_mmio_fits(shMem) && radeon_mmio_fits(shBases)
		&& radeon_mmio_fits(grbmGfx)) {
		savedGrbmGfx = Read32(OUT, grbmGfx);
		savedShMem = Read32(OUT, shMem);
		savedShBases = Read32(OUT, shBases);
		Write32(OUT, grbmGfx, 0);
		Write32(OUT, shMem, (3u << 3) | (1u << 12));
		Write32(OUT, shBases, 0);
		pipePrepared = true;
		ERROR("%s: R5 radeon_hd: GFX9 GRBM_GFX_CNTL 0x%" B_PRIx32
			" -> 0 SH_MEM 0x%" B_PRIx32 " -> 0x%" B_PRIx32
			" bases 0x%" B_PRIx32 "\n", __func__, savedGrbmGfx, savedShMem,
			Read32(OUT, shMem), savedShBases);
	}
	if (!radeon_gfx9_verify_ring_config(cntl, ringGpu, location, top,
			ringDoorbell, mecStarted, pipePrepared, aperBefore)) {
		Write32(OUT, meCntl, savedMe | haltMask);
		if (pipePrepared) {
			Write32(OUT, grbmGfx, 0);
			Write32(OUT, shMem, savedShMem);
			Write32(OUT, shBases, savedShBases);
			Write32(OUT, grbmGfx, savedGrbmGfx);
		}
		if (aperBefore != 0xffffffff)
			Write32(OUT, kNbioDoorbellAper, aperBefore);
		Write32(OUT, rbBase, savedBase);
		Write32(OUT, rbBaseHi, savedBaseHi);
		Write32(OUT, rbCntl, savedCntl);
		Write32(OUT, rbWptr, savedWptr);
		Write32(OUT, rbWptrHi, savedWptrHi);
		Write32(OUT, rbDoorbell, savedDoorbell);
		Write32(OUT, rbRangeLo, savedRangeLo);
		Write32(OUT, rbRangeHi, savedRangeHi);
		Write32(OUT, maxContext, savedContext);
		Write32(OUT, deviceId, savedDevice);
		Write32(OUT, rbDelay, savedDelay);
		Write32(OUT, rbVmid, savedVmid);
		Write32(OUT, rbRptrAddr, savedRptrAddr);
		Write32(OUT, rbRptrAddrHi, savedRptrAddrHi);
		Write32(OUT, rbPollLo, savedPollLo);
		Write32(OUT, rbPollHi, savedPollHi);
		Write32(OUT, meCntl, savedMe);
		if (mecStarted)
			Write32(OUT, mecCntl, savedMec);
		radeon_gfx9_restore_saved(rlcTouched ? rlcBios : rlcBefore,
			rlcTouched || rlcStarted, rlcTouched ? rlcClockBios : rlcClock);
		radeon_gfx9_publish(pspOwnsLoad, rlcStarted, rlcBefore, rlcAfter,
			rlcVersion, scratchRead, false, true,
			(uint32)sRadeonGfx9Ring.submitted,
			rptrBefore, rptrBefore);
		radeon_gfx9_note_doorbell(ringDoorbell, Read32(OUT, rbDoorbell), 0, 0,
			0, 0, aperBefore, aperAfter, sdma0Range, sdma1Range, ihRange,
			mmschRange);
		radeon_gfx9_stall_note();
		ERROR("%s: R5 radeon_hd: GFX9 scratch stayed 0x%" B_PRIx32
			" rptr 0x%" B_PRIx32 " dwords %" B_PRIu32 ", ring restored\n",
			__func__, scratchRead, rptrBefore,
			(uint32)sRadeonGfx9Ring.submitted);
		return;
	}
	Write32(OUT, meCntl, (savedMe | haltMask) & ~haltMask);
	snooze(1000);
	uint32 meNow = Read32(OUT, meCntl);
	ERROR("%s: R5 radeon_hd: GFX9 ring gpu 0x%" B_PRIx64
		" CP_ME_CNTL 0x%" B_PRIx32 " rptr 0x%" B_PRIx32
		" -> 0x%" B_PRIx32 "\n", __func__, ringGpu, meNow, rptrBefore,
		rptrZero);
	radeon_visible_barrier();
	uint64 wptr = sRadeonGfx9Ring.submitted;
	// gfx_v9_0_ring_set_wptr_gfx stores the count in the poll slot
	// before it rings. One doorbell. The value is not written again.
	*(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + 512) = wptr;
	radeon_gfx9_flush_visible(sRadeonGfx9Ring.fenceAddress, 4096);
	radeon_visible_barrier();
	uint64 pollRead = *(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + 512);
	uint32 doorbellNow = Read32(OUT, rbDoorbell);
	if (ringDoorbell) {
		*(volatile uint64*)(sHost->doorbell + kGfx9DoorbellByte) = wptr;
		radeon_visible_barrier();
	} else {
		Write32(OUT, rbWptr, (uint32)wptr);
		Write32(OUT, rbWptrHi, (uint32)(wptr >> 32));
	}
	uint32 rptrAtBell = Read32(OUT, rbRptr);
	uint32 rptrWbAtBell = *(volatile uint32*)(sRadeonGfx9Ring.fenceAddress + 256);
	ERROR("%s: R5 radeon_hd: GFX9 after doorbell rptr wb 0x%" B_PRIx32
		" mmio 0x%" B_PRIx32 "\n", __func__, rptrWbAtBell, rptrAtBell);

	uint32 scratchNow = 0xCAFEDEAD;
	for (int i = 0; i < 100; i++) {
		scratchNow = Read32(OUT, scratch);
		if (scratchNow == 0xDEADBEEF)
			break;
		snooze(1000);
	}
	uint32 rptrNow = Read32(OUT, rbRptr);
	uint32 rptrWb = *(volatile uint32*)(sRadeonGfx9Ring.fenceAddress + 256);
	ERROR("%s: R5 radeon_hd: GFX9 doorbell %s ctl 0x%" B_PRIx32
		" poll 0x%" B_PRIx64 " rptr wb 0x%" B_PRIx32 " mmio 0x%" B_PRIx32
		"\n", __func__, ringDoorbell ? "yes" : "no", doorbellNow, pollRead,
		rptrWb, rptrNow);
	radeon_gfx9_capture_stall();
	uint32 landedDwords = (uint32)sRadeonGfx9Ring.submitted;
	if (scratchNow == 0xDEADBEEF) {
		radeon_gfx9_publish(pspOwnsLoad, rlcStarted, rlcBefore, rlcAfter,
			rlcVersion, scratchNow, true, true, landedDwords,
			rptrNow, rptrBefore);
		radeon_gfx9_note_doorbell(ringDoorbell, doorbellNow, pollRead, rptrWb,
			rptrAtBell, rptrWbAtBell, aperBefore, aperAfter, sdma0Range,
			sdma1Range, ihRange, mmschRange);
		radeon_gfx9_stall_note();
		ERROR("%s: R5 radeon_hd: GFX9 scratch 0x%" B_PRIx32
			" rptr 0x%" B_PRIx32 " dwords %" B_PRIu32 ", fence landed\n",
			__func__, scratchNow, rptrNow, landedDwords);
		sK0Ok = true;
		if (sGfx9Compute) {
			radeon_gfx9_compute(location, fenceGpu, ringDoorbell, meCntl,
				haltMask);
		} else if (sGfx9Workload) {
			radeon_gfx9_workload(location, ringGpu, fenceGpu, ringDoorbell,
				meCntl, haltMask);
		}
		return;
	}

	Write32(OUT, meCntl, savedMe | haltMask);
	if (pipePrepared) {
		Write32(OUT, grbmGfx, 0);
		Write32(OUT, shMem, savedShMem);
		Write32(OUT, shBases, savedShBases);
		Write32(OUT, grbmGfx, savedGrbmGfx);
	}
	if (aperBefore != 0xffffffff)
		Write32(OUT, kNbioDoorbellAper, aperBefore);
	Write32(OUT, rbBase, savedBase);
	Write32(OUT, rbBaseHi, savedBaseHi);
	Write32(OUT, rbCntl, savedCntl);
	Write32(OUT, rbWptr, savedWptr);
	Write32(OUT, rbWptrHi, savedWptrHi);
	Write32(OUT, rbDoorbell, savedDoorbell);
	Write32(OUT, rbRangeLo, savedRangeLo);
	Write32(OUT, rbRangeHi, savedRangeHi);
	Write32(OUT, maxContext, savedContext);
	Write32(OUT, deviceId, savedDevice);
	Write32(OUT, rbDelay, savedDelay);
	Write32(OUT, rbVmid, savedVmid);
	Write32(OUT, rbRptrAddr, savedRptrAddr);
	Write32(OUT, rbRptrAddrHi, savedRptrAddrHi);
	Write32(OUT, rbPollLo, savedPollLo);
	Write32(OUT, rbPollHi, savedPollHi);
	Write32(OUT, meCntl, savedMe);
	if (mecStarted)
		Write32(OUT, mecCntl, savedMec);
	radeon_gfx9_restore_saved(rlcTouched ? rlcBios : rlcBefore,
		rlcTouched || rlcStarted, rlcTouched ? rlcClockBios : rlcClock);
	radeon_gfx9_publish(pspOwnsLoad, rlcStarted, rlcBefore, rlcAfter,
		rlcVersion, scratchNow, false, true,
		(uint32)sRadeonGfx9Ring.submitted,
		rptrNow, rptrBefore);
	radeon_gfx9_note_doorbell(ringDoorbell, doorbellNow, pollRead, rptrWb,
		rptrAtBell, rptrWbAtBell, aperBefore, aperAfter, sdma0Range,
		sdma1Range, ihRange, mmschRange);
	radeon_gfx9_stall_note();
	ERROR("%s: R5 radeon_hd: GFX9 scratch stayed 0x%" B_PRIx32
		" rptr 0x%" B_PRIx32 " dwords %" B_PRIu32 ", ring restored\n",
		__func__, scratchNow, rptrNow, (uint32)sRadeonGfx9Ring.submitted);
}


void
r5_gfx9_prepare(r5_gfx9_host* host, uint64 vramBytes)
{
	sHost = host;
	sVramBytes = vramBytes;
	radeon_gfx9_prepare_ring();
}


bool
r5_gfx9_start(r5_gfx9_host* host, uint32 pciId, uint64 vramBytes,
	bool workload, bool compute, r5_gfx9_results* results)
{
	sHost = host;
	sPciId = pciId;
	sVramBytes = vramBytes;
	sResults = results;
	sGfx9Workload = workload;
	sGfx9Compute = compute;
	sK0Ok = false;
	radeon_gfx9_cp_start();
	return sK0Ok;
}


void
r5_gfx9_note(const char* line)
{
	r5_text_mode("a");
	r5_text("%s\n", line);
}


static uint32
r5_next_sequence()
{
	uint64 seen = *(volatile uint64*)(sRadeonGfx9Ring.fenceAddress
		+ kFenceSequence);
	return (uint32)seen + 1u;
}


static uint64
r5_ring_rptr()
{
	uint32 reg = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmCP_RB0_RPTR));
	if (sRadeonGfx9Ring.fenceAddress == NULL)
		return reg;
	uint64 wb = *(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + 256);
	if (wb != 0)
		return wb;
	return reg;
}


static bool
r5_ring_has_room(uint32 count)
{
	uint32 dwords = sRadeonGfx9Ring.ringBytes / sizeof(uint32);
	if (sRadeonGfx9Ring.ringAddress == NULL || dwords < 2
		|| (dwords & (dwords - 1u)) != 0)
		return false;
	uint64 wptr = sRadeonGfx9Ring.submitted;
	uint64 rptr = r5_gfx9_rptr_count(dwords, wptr, r5_ring_rptr());
	if (wptr >= rptr && (wptr - rptr) > sRingMaxFill)
		sRingMaxFill = (uint32)(wptr - rptr);
	return (uint64)count <= r5_gfx9_ring_space(dwords, wptr, rptr);
}


void
r5_gfx9_ring_waited()
{
	sRingWaits++;
}


void
r5_gfx9_ring_fill(uint32* waits, uint32* maxFill, uint32* dwords)
{
	uint32 length = 1024;
	if (sRadeonGfx9Ring.ringBytes >= sizeof(uint32)) {
		uint32 count = sRadeonGfx9Ring.ringBytes / sizeof(uint32);
		if (count >= 2 && (count & (count - 1u)) == 0)
			length = count;
	}
	if (waits != NULL)
		*waits = sRingWaits;
	if (maxFill != NULL)
		*maxFill = sRingMaxFill;
	if (dwords != NULL)
		*dwords = length;
}


void
r5_gfx9_ring_ptrs(uint64* wptr, uint64* rptrOffset)
{
	if (wptr != NULL)
		*wptr = sRadeonGfx9Ring.submitted;
	if (rptrOffset != NULL)
		*rptrOffset = r5_ring_rptr();
}


static bool
r5_ring_doorbell()
{
	return sHost->doorbell != NULL
		&& sHost->doorbellBytes >= (uint64)kGfx9DoorbellByte + sizeof(uint64);
}


bool
r5_gfx9_k1_submit(uint32* _sequence)
{
	if (!sK0Ok || sRadeonGfx9Ring.fenceAddress == NULL || !r5_ring_has_room(12))
		return false;
	const uint32 meCntl = radeon_gfx9_byte(kGfx9Seg0, mmCP_ME_CNTL);
	const uint32 haltMask = radeon_gfx9_halt_mask();
	uint32 location = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_BASE));
	uint64 fenceGpu = radeon_gfx9_mc_addr(location, sRadeonGfx9Ring.fenceOffset);
	uint64 ibOffset = 0;
	uint8* ibAddress = NULL;
	if (r5_vram(4096, 4096, &ibOffset, &ibAddress) != B_OK || ibAddress == NULL)
		return false;
	uint64 ibGpu = radeon_gfx9_mc_addr(location, ibOffset);
	uint64 wordGpu = fenceGpu + kFenceIbWord;
	uint64 sequenceGpu = fenceGpu + kFenceSequence;
	uint64 sequence = r5_next_sequence();
	uint32* ib = (uint32*)ibAddress;
	ib[0] = radeon_packet3(kWlWriteData, 3);
	ib[1] = kWlWriteDstMemory | kWlWrConfirm;
	ib[2] = (uint32)wordGpu;
	ib[3] = (uint32)(wordGpu >> 32);
	ib[4] = 0xDEADBEEF;
	radeon_gfx9_flush_visible(ibAddress, 4096);
	radeon_visible_barrier();

	uint32 packets[12];
	packets[0] = radeon_packet3(kWlIndirectBuffer, 2);
	packets[1] = (uint32)ibGpu & ~3u;
	packets[2] = (uint32)((ibGpu >> 32) & 0xffffu);
	packets[3] = kWlIbValid | 5u;
	packets[4] = radeon_packet3(kWlReleaseMem, 6);
	packets[5] = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| kWlCacheFlushTs | (5u << 8);
	// DATA_SEL(1) is bit 29, the landed fence. INT_SEL(2) is bits 25:24
	// of that same dword (PACKET3_RELEASE_MEM_INT_SEL).
	packets[6] = (1u << 29) | (2u << 24);
	packets[7] = (uint32)sequenceGpu;
	packets[8] = (uint32)(sequenceGpu >> 32);
	packets[9] = (uint32)sequence;
	packets[10] = (uint32)(sequence >> 32);
	packets[11] = 0;
	if (!radeon_gfx9_submit(packets, 12, sequence, r5_ring_doorbell(), meCntl,
			haltMask))
		return false;
	if (_sequence != NULL)
		*_sequence = sequence;
	return true;
}


bool
r5_gfx9_k2_dma(uint64 destMc, uint32* _sequence)
{
	if (!sK0Ok || sRadeonGfx9Ring.fenceAddress == NULL || !r5_ring_has_room(12))
		return false;
	const uint32 meCntl = radeon_gfx9_byte(kGfx9Seg0, mmCP_ME_CNTL);
	const uint32 haltMask = radeon_gfx9_halt_mask();
	uint32 location = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_BASE));
	uint64 fenceGpu = radeon_gfx9_mc_addr(location, sRadeonGfx9Ring.fenceOffset);
	uint64 ibOffset = 0;
	uint8* ibAddress = NULL;
	if (r5_vram(4096, 4096, &ibOffset, &ibAddress) != B_OK || ibAddress == NULL)
		return false;
	uint32* ib = (uint32*)ibAddress;
	uint32 ibDwords = radeon_gfx9_dma_ib(ib, fenceGpu, destMc, 1, kK2Pattern);
	radeon_gfx9_flush_visible(ibAddress, 4096);
	radeon_visible_barrier();
	uint64 ibGpu = radeon_gfx9_mc_addr(location, ibOffset);
	uint64 sequenceGpu = fenceGpu + kFenceSequence;
	uint64 sequence = r5_next_sequence();
	uint32 packets[12];
	packets[0] = radeon_packet3(kWlIndirectBuffer, 2);
	packets[1] = (uint32)ibGpu & ~3u;
	packets[2] = (uint32)((ibGpu >> 32) & 0xffffu);
	packets[3] = kWlIbValid | ibDwords;
	packets[4] = radeon_packet3(kWlReleaseMem, 6);
	packets[5] = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| kWlCacheFlushTs | (5u << 8);
	packets[6] = 1u << 29;
	packets[7] = (uint32)sequenceGpu;
	packets[8] = (uint32)(sequenceGpu >> 32);
	packets[9] = (uint32)sequence;
	packets[10] = (uint32)(sequence >> 32);
	packets[11] = 0;
	if (!radeon_gfx9_submit(packets, 12, sequence, r5_ring_doorbell(), meCntl,
			haltMask))
		return false;
	if (_sequence != NULL)
		*_sequence = sequence;
	return true;
}


bool
r5_gfx9_k2_gart_ib(uint64 ibMc, uint8* ibCpu, uint32* _sequence)
{
	if (!sK0Ok || ibCpu == NULL || sRadeonGfx9Ring.fenceAddress == NULL
		|| !r5_ring_has_room(12))
		return false;
	const uint32 meCntl = radeon_gfx9_byte(kGfx9Seg0, mmCP_ME_CNTL);
	const uint32 haltMask = radeon_gfx9_halt_mask();
	uint32 location = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, mmMC_VM_FB_LOCATION_BASE));
	uint64 fenceGpu = radeon_gfx9_mc_addr(location, sRadeonGfx9Ring.fenceOffset);
	uint64 wordGpu = fenceGpu + kFenceIbWord;
	uint64 sequenceGpu = fenceGpu + kFenceSequence;
	uint64 sequence = r5_next_sequence();
	uint32* ib = (uint32*)ibCpu;
	ib[0] = radeon_packet3(kWlWriteData, 3);
	ib[1] = kWlWriteDstMemory | kWlWrConfirm;
	ib[2] = (uint32)wordGpu;
	ib[3] = (uint32)(wordGpu >> 32);
	ib[4] = 0xDEADBEEF;
	radeon_gfx9_flush_visible(ibCpu, 64);
	radeon_visible_barrier();
	uint32 packets[12];
	packets[0] = radeon_packet3(kWlIndirectBuffer, 2);
	packets[1] = (uint32)ibMc & ~3u;
	packets[2] = (uint32)((ibMc >> 32) & 0xffffu);
	packets[3] = kWlIbValid | 5u;
	packets[4] = radeon_packet3(kWlReleaseMem, 6);
	packets[5] = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| kWlCacheFlushTs | (5u << 8);
	packets[6] = 1u << 29;
	packets[7] = (uint32)sequenceGpu;
	packets[8] = (uint32)(sequenceGpu >> 32);
	packets[9] = (uint32)sequence;
	packets[10] = (uint32)(sequence >> 32);
	packets[11] = 0;
	*(volatile uint32*)(sRadeonGfx9Ring.fenceAddress + kFenceIbWord) = 0;
	if (!radeon_gfx9_submit(packets, 12, sequence, r5_ring_doorbell(), meCntl,
			haltMask))
		return false;
	if (_sequence != NULL)
		*_sequence = sequence;
	return true;
}


uint32
r5_gfx9_last_word()
{
	if (sRadeonGfx9Ring.fenceAddress == NULL)
		return 0;
	return *(volatile uint32*)(sRadeonGfx9Ring.fenceAddress + kFenceIbWord);
}


uint64
r5_gfx9_last_sequence()
{
	if (sRadeonGfx9Ring.fenceAddress == NULL)
		return 0;
	return *(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + kFenceSequence);
}
