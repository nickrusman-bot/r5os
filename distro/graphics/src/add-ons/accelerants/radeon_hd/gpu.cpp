/*
 * Copyright 2006-2011, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexander von Gluck, kallisti5@unixzen.com
 *		Axel Dörfler, axeld@pinc-software.de
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The Haiku copyright and the MIT License above still
 * cover the original work. A build that ships both follows GPL-3.0-or-later,
 * and the MIT notices stay on the Haiku portions.
 */



#include "gpu.h"

#include <Debug.h>
#include <FindDirectory.h>
#include <OS.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "accelerant_protos.h"
#include "accelerant.h"
#include "atom.h"
#include "bios.h"
#include "pll.h"
#include "utility.h"


#undef TRACE

#define TRACE_GPU
#ifdef TRACE_GPU
#   define TRACE(x...) _sPrintf("radeon_hd: " x)
#else
#   define TRACE(x...) ;
#endif

#define ERROR(x...) _sPrintf("radeon_hd: " x)


status_t
radeon_gpu_probe()
{
	uint8 tableMajor;
	uint8 tableMinor;
	uint16 tableOffset;

	gInfo->displayClockFrequency = 0;
	gInfo->dpExternalClock = 0;

	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);
	if (atom_parse_data_header(gAtomContext, index, NULL,
		&tableMajor, &tableMinor, &tableOffset) != B_OK) {
		ERROR("%s: Couldn't parse data header\n", __func__);
		return B_ERROR;
	}

	TRACE("%s: table %" B_PRIu8 ".%" B_PRIu8 "\n", __func__,
		tableMajor, tableMinor);

	union atomFirmwareInfo {
		ATOM_FIRMWARE_INFO info;
		ATOM_FIRMWARE_INFO_V1_2 info_12;
		ATOM_FIRMWARE_INFO_V1_3 info_13;
		ATOM_FIRMWARE_INFO_V1_4 info_14;
		ATOM_FIRMWARE_INFO_V2_1 info_21;
		ATOM_FIRMWARE_INFO_V2_2 info_22;
	};
	union atomFirmwareInfo* firmwareInfo
		= (union atomFirmwareInfo*)(gAtomContext->bios + tableOffset);

	radeon_shared_info &info = *gInfo->shared_info;

	if (info.dceMajor >= 4) {
		gInfo->displayClockFrequency = B_LENDIAN_TO_HOST_INT32(
			firmwareInfo->info_21.ulDefaultDispEngineClkFreq);
		gInfo->displayClockFrequency *= 10;
		if (gInfo->displayClockFrequency == 0) {
			if (info.dceMajor == 5)
				gInfo->displayClockFrequency = 540000;
			else
				gInfo->displayClockFrequency = 600000;
		}
		gInfo->dpExternalClock = B_LENDIAN_TO_HOST_INT16(
			firmwareInfo->info_21.usUniphyDPModeExtClkFreq);
		gInfo->dpExternalClock *= 10;
	}

	gInfo->maximumPixelClock = B_LENDIAN_TO_HOST_INT16(
		firmwareInfo->info.usMaxPixelClock);
	gInfo->maximumPixelClock *= 10;

	if (gInfo->maximumPixelClock == 0)
		gInfo->maximumPixelClock = 400000;

	return B_OK;
}


status_t
radeon_gpu_reset()
{
	radeon_shared_info &info = *gInfo->shared_info;

	// Read GRBM Command Processor status
	if ((Read32(OUT, GRBM_STATUS) & GUI_ACTIVE) == 0)
		return B_ERROR;

	TRACE("%s: GPU software reset in progress...\n", __func__);

	// Halt memory controller
	struct gpu_state gpuState;
	radeon_gpu_mc_halt(&gpuState);

	if (radeon_gpu_mc_idlewait() != B_OK)
		ERROR("%s: Couldn't idle memory controller!\n", __func__);

	if (info.chipsetID < RADEON_CEDAR) {
		Write32(OUT, CP_ME_CNTL, CP_ME_HALT);
			// Disable Command Processor parsing / prefetching

		// Register busy masks for early Radeon HD cards

		// GRBM Command Processor Status
		uint32 grbmBusyMask = VC_BUSY;
			// Vertex Cache Busy
		grbmBusyMask |= VGT_BUSY_NO_DMA | VGT_BUSY;
			// Vertex Grouper Tessellator Busy
		grbmBusyMask |= TA03_BUSY;
			// unknown
		grbmBusyMask |= TC_BUSY;
			// Texture Cache Busy
		grbmBusyMask |= SX_BUSY;
			// Shader Export Busy
		grbmBusyMask |= SH_BUSY;
			// Sequencer Instruction Cache Busy
		grbmBusyMask |= SPI_BUSY;
			// Shader Processor Interpolator Busy
		grbmBusyMask |= SMX_BUSY;
			// Shader Memory Exchange
		grbmBusyMask |= SC_BUSY;
			// Scan Converter Busy
		grbmBusyMask |= PA_BUSY;
			// Primitive Assembler Busy
		grbmBusyMask |= DB_BUSY;
			// Depth Block Busy
		grbmBusyMask |= CR_BUSY;
			// unknown
		grbmBusyMask |= CB_BUSY;
			// Color Block Busy
		grbmBusyMask |= GUI_ACTIVE;
			// unknown (graphics pipeline active?)

		// GRBM Command Processor Detailed Status
		uint32 grbm2BusyMask = SPI0_BUSY | SPI1_BUSY | SPI2_BUSY | SPI3_BUSY;
			// Shader Processor Interpolator 0 - 3 Busy
		grbm2BusyMask |= TA0_BUSY | TA1_BUSY | TA2_BUSY | TA3_BUSY;
			// unknown 0 - 3 Busy
		grbm2BusyMask |= DB0_BUSY | DB1_BUSY | DB2_BUSY | DB3_BUSY;
			// Depth Block 0 - 3 Busy
		grbm2BusyMask |= CB0_BUSY | CB1_BUSY | CB2_BUSY | CB3_BUSY;
			// Color Block 0 - 3 Busy

		uint32 tmp;
		/* Check if any of the rendering block is busy and reset it */
		if ((Read32(OUT, GRBM_STATUS) & grbmBusyMask) != 0
			|| (Read32(OUT, GRBM_STATUS2) & grbm2BusyMask) != 0) {
			tmp = SOFT_RESET_CR
				| SOFT_RESET_DB
				| SOFT_RESET_CB
				| SOFT_RESET_PA
				| SOFT_RESET_SC
				| SOFT_RESET_SMX
				| SOFT_RESET_SPI
				| SOFT_RESET_SX
				| SOFT_RESET_SH
				| SOFT_RESET_TC
				| SOFT_RESET_TA
				| SOFT_RESET_VC
				| SOFT_RESET_VGT;
			Write32(OUT, GRBM_SOFT_RESET, tmp);
			Read32(OUT, GRBM_SOFT_RESET);
			snooze(15000);
			Write32(OUT, GRBM_SOFT_RESET, 0);
		}

		// Reset CP
		tmp = SOFT_RESET_CP;
		Write32(OUT, GRBM_SOFT_RESET, tmp);
		Read32(OUT, GRBM_SOFT_RESET);
		snooze(15000);
		Write32(OUT, GRBM_SOFT_RESET, 0);

		// Let things settle
		snooze(1000);
	} else {
		// Evergreen and higher

		Write32(OUT, CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT);
			// Disable Command Processor parsing / prefetching

		// reset the graphics pipeline components
		uint32 grbmReset = (SOFT_RESET_CP
			| SOFT_RESET_CB
			| SOFT_RESET_DB
			| SOFT_RESET_GDS
			| SOFT_RESET_PA
			| SOFT_RESET_SC
			| SOFT_RESET_SPI
			| SOFT_RESET_SH
			| SOFT_RESET_SX
			| SOFT_RESET_TC
			| SOFT_RESET_TA
			| SOFT_RESET_VGT
			| SOFT_RESET_IA);

		Write32(OUT, GRBM_SOFT_RESET, grbmReset);
		Read32(OUT, GRBM_SOFT_RESET);

		snooze(50);
		Write32(OUT, GRBM_SOFT_RESET, 0);
		Read32(OUT, GRBM_SOFT_RESET);
		snooze(50);
	}

	// Resume memory controller
	radeon_gpu_mc_resume(&gpuState);
	return B_OK;
}


status_t
radeon_gpu_quirks()
{
	radeon_shared_info &info = *gInfo->shared_info;

	// Fix PCIe power distribution issue for Polaris10 XT
	// aka "Card draws >75W from PCIe bus"
	if (info.chipsetID == RADEON_POLARIS10 && info.pciRev == 0xc7) {
		ERROR("%s: Applying Polaris10 power distribution fix.\n",
			__func__);
		radeon_gpu_i2c_cmd(0x10, 0x96, 0x1e, 0xdd);
		radeon_gpu_i2c_cmd(0x10, 0x96, 0x1f, 0xd0);
	}

	return B_OK;
}


status_t
radeon_gpu_i2c_cmd(uint16 slaveAddr, uint16 lineNumber, uint8 offset,
	uint8 data)
{
	TRACE("%s\n", __func__);

	PROCESS_I2C_CHANNEL_TRANSACTION_PS_ALLOCATION args;
	memset(&args, 0, sizeof(args));

	int index = GetIndexIntoMasterTable(COMMAND,
		ProcessI2cChannelTransaction);

	args.ucRegIndex = offset;
	args.lpI2CDataOut = data;
	args.ucFlag = HW_I2C_WRITE;
	args.ucI2CSpeed = TARGET_HW_I2C_CLOCK;
	args.ucTransBytes = 1;
	args.ucSlaveAddr = slaveAddr;
	args.ucLineNumber = lineNumber;

	atom_execute_table(gAtomContext, index, (uint32*)&args);
	return B_OK;
}


void
radeon_gpu_mc_halt(gpu_state* gpuState)
{
	// Backup current memory controller state
	gpuState->d1vgaControl = Read32(OUT, AVIVO_D1VGA_CONTROL);
	gpuState->d2vgaControl = Read32(OUT, AVIVO_D2VGA_CONTROL);
	gpuState->vgaRenderControl = Read32(OUT, AVIVO_VGA_RENDER_CONTROL);
	gpuState->vgaHdpControl = Read32(OUT, AVIVO_VGA_HDP_CONTROL);
	gpuState->d1crtcControl = Read32(OUT, AVIVO_D1CRTC_CONTROL);
	gpuState->d2crtcControl = Read32(OUT, AVIVO_D2CRTC_CONTROL);

	// halt all memory controller actions
	Write32(OUT, AVIVO_VGA_RENDER_CONTROL, 0);
	Write32(OUT, AVIVO_D1CRTC_UPDATE_LOCK, 1);
	Write32(OUT, AVIVO_D2CRTC_UPDATE_LOCK, 1);
	Write32(OUT, AVIVO_D1CRTC_CONTROL, 0);
	Write32(OUT, AVIVO_D2CRTC_CONTROL, 0);
	Write32(OUT, AVIVO_D1CRTC_UPDATE_LOCK, 0);
	Write32(OUT, AVIVO_D2CRTC_UPDATE_LOCK, 0);
	Write32(OUT, AVIVO_D1VGA_CONTROL, 0);
	Write32(OUT, AVIVO_D2VGA_CONTROL, 0);
}


void
radeon_gpu_mc_resume(gpu_state* gpuState)
{
	Write32(OUT, AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS, gInfo->fb.vramStart);
	Write32(OUT, AVIVO_D1GRPH_SECONDARY_SURFACE_ADDRESS, gInfo->fb.vramStart);
	Write32(OUT, AVIVO_D2GRPH_PRIMARY_SURFACE_ADDRESS, gInfo->fb.vramStart);
	Write32(OUT, AVIVO_D2GRPH_SECONDARY_SURFACE_ADDRESS, gInfo->fb.vramStart);
	// TODO: Evergreen high surface addresses?
	Write32(OUT, AVIVO_VGA_MEMORY_BASE_ADDRESS, gInfo->fb.vramStart);

	// Unlock host access
	Write32(OUT, AVIVO_VGA_HDP_CONTROL, gpuState->vgaHdpControl);
	snooze(1);

	// Restore memory controller state
	Write32(OUT, AVIVO_D1VGA_CONTROL, gpuState->d1vgaControl);
	Write32(OUT, AVIVO_D2VGA_CONTROL, gpuState->d2vgaControl);
	Write32(OUT, AVIVO_D1CRTC_UPDATE_LOCK, 1);
	Write32(OUT, AVIVO_D2CRTC_UPDATE_LOCK, 1);
	Write32(OUT, AVIVO_D1CRTC_CONTROL, gpuState->d1crtcControl);
	Write32(OUT, AVIVO_D2CRTC_CONTROL, gpuState->d2crtcControl);
	Write32(OUT, AVIVO_D1CRTC_UPDATE_LOCK, 0);
	Write32(OUT, AVIVO_D2CRTC_UPDATE_LOCK, 0);
	Write32(OUT, AVIVO_VGA_RENDER_CONTROL, gpuState->vgaRenderControl);
}


status_t
radeon_gpu_mc_idlewait()
{
	uint32 idleStatus;

	uint32 busyBits
		= (VMC_BUSY | MCB_BUSY | MCDZ_BUSY | MCDY_BUSY | MCDX_BUSY | MCDW_BUSY);

	uint32 tryCount;
	// We give the gpu 0.5 seconds to become idle checking once every 500usec
	for (tryCount = 0; tryCount < 1000; tryCount++) {
		if (((idleStatus = Read32(MC, SRBM_STATUS)) & busyBits) == 0)
			return B_OK;
		snooze(500);
	}

	ERROR("%s: Couldn't idle SRBM!\n", __func__);

	bool state;
	state = (idleStatus & VMC_BUSY) != 0;
	TRACE("%s: VMC is %s\n", __func__, state ? "busy" : "idle");
	state = (idleStatus & MCB_BUSY) != 0;
	TRACE("%s: MCB is %s\n", __func__, state ? "busy" : "idle");
	state = (idleStatus & MCDZ_BUSY) != 0;
	TRACE("%s: MCDZ is %s\n", __func__, state ? "busy" : "idle");
	state = (idleStatus & MCDY_BUSY) != 0;
	TRACE("%s: MCDY is %s\n", __func__, state ? "busy" : "idle");
	state = (idleStatus & MCDX_BUSY) != 0;
	TRACE("%s: MCDX is %s\n", __func__, state ? "busy" : "idle");
	state = (idleStatus & MCDW_BUSY) != 0;
	TRACE("%s: MCDW is %s\n", __func__, state ? "busy" : "idle");

	return B_TIMED_OUT;
}


static status_t
radeon_gpu_mc_setup_r600()
{
	// HDP initialization
	uint32 i;
	uint32 j;
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		Write32(OUT, (0x2c14 + j), 0x00000000);
		Write32(OUT, (0x2c18 + j), 0x00000000);
		Write32(OUT, (0x2c1c + j), 0x00000000);
		Write32(OUT, (0x2c20 + j), 0x00000000);
		Write32(OUT, (0x2c24 + j), 0x00000000);
	}
	Write32(OUT, R600_HDP_REG_COHERENCY_FLUSH_CNTL, 0);

	// idle the memory controller
	struct gpu_state gpuState;
	radeon_gpu_mc_halt(&gpuState);

	if (radeon_gpu_mc_idlewait() != B_OK)
		ERROR("%s: Modifying non-idle memory controller!\n", __func__);

	// TODO: Memory Controller AGP
	Write32(OUT, R600_MC_VM_SYSTEM_APERTURE_LOW_ADDR,
		gInfo->fb.vramStart >> 12);
	Write32(OUT, R600_MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
		gInfo->fb.vramEnd >> 12);

	Write32(OUT, R600_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR, 0);
	uint32 tmp = ((gInfo->fb.vramEnd >> 24) & 0xFFFF) << 16;
	tmp |= ((gInfo->fb.vramStart >> 24) & 0xFFFF);

	Write32(OUT, R600_MC_VM_FB_LOCATION, tmp);
	Write32(OUT, R600_HDP_NONSURFACE_BASE, (gInfo->fb.vramStart >> 8));
	Write32(OUT, R600_HDP_NONSURFACE_INFO, (2 << 7));
	Write32(OUT, R600_HDP_NONSURFACE_SIZE, 0x3FFFFFFF);

	// is AGP?
	//	Write32(OUT, R600_MC_VM_AGP_TOP, gInfo->fb.gartEnd >> 22);
	//	Write32(OUT, R600_MC_VM_AGP_BOT, gInfo->fb.gartStart >> 22);
	//	Write32(OUT, R600_MC_VM_AGP_BASE, gInfo->fb.agpBase >> 22);
	// else?
	Write32(OUT, R600_MC_VM_AGP_BASE, 0);
	Write32(OUT, R600_MC_VM_AGP_TOP, 0x0FFFFFFF);
	Write32(OUT, R600_MC_VM_AGP_BOT, 0x0FFFFFFF);

	if (radeon_gpu_mc_idlewait() != B_OK)
		ERROR("%s: Modifying non-idle memory controller!\n", __func__);

	radeon_gpu_mc_resume(&gpuState);

	// disable render control
	Write32(OUT, 0x000300, Read32(OUT, 0x000300) & 0xFFFCFFFF);

	return B_OK;
}


static status_t
radeon_gpu_mc_setup_r700()
{
	// HDP initialization
	uint32 i;
	uint32 j;
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		Write32(OUT, (0x2c14 + j), 0x00000000);
		Write32(OUT, (0x2c18 + j), 0x00000000);
		Write32(OUT, (0x2c1c + j), 0x00000000);
		Write32(OUT, (0x2c20 + j), 0x00000000);
		Write32(OUT, (0x2c24 + j), 0x00000000);
	}

	// On r7xx read from HDP_DEBUG1 vs write HDP_REG_COHERENCY_FLUSH_CNTL
	Read32(OUT, R700_HDP_DEBUG1);

	// idle the memory controller
	struct gpu_state gpuState;
	radeon_gpu_mc_halt(&gpuState);

	if (radeon_gpu_mc_idlewait() != B_OK)
		ERROR("%s: Modifying non-idle memory controller!\n", __func__);

	Write32(OUT, AVIVO_VGA_HDP_CONTROL, AVIVO_VGA_MEMORY_DISABLE);

	// TODO: Memory Controller AGP
	Write32(OUT, R700_MC_VM_SYSTEM_APERTURE_LOW_ADDR,
		gInfo->fb.vramStart >> 12);
	Write32(OUT, R700_MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
		gInfo->fb.vramEnd >> 12);

	Write32(OUT, R700_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR, 0);
	uint32 tmp = ((gInfo->fb.vramEnd >> 24) & 0xFFFF) << 16;
	tmp |= ((gInfo->fb.vramStart >> 24) & 0xFFFF);

	Write32(OUT, R700_MC_VM_FB_LOCATION, tmp);
	Write32(OUT, R700_HDP_NONSURFACE_BASE, (gInfo->fb.vramStart >> 8));
	Write32(OUT, R700_HDP_NONSURFACE_INFO, (2 << 7));
	Write32(OUT, R700_HDP_NONSURFACE_SIZE, 0x3FFFFFFF);

	// is AGP?
	//	Write32(OUT, R700_MC_VM_AGP_TOP, gInfo->fb.gartEnd >> 22);
	//	Write32(OUT, R700_MC_VM_AGP_BOT, gInfo->fb.gartStart >> 22);
	//	Write32(OUT, R700_MC_VM_AGP_BASE, gInfo->fb.agpBase >> 22);
	// else?
	Write32(OUT, R700_MC_VM_AGP_BASE, 0);
	Write32(OUT, R700_MC_VM_AGP_TOP, 0x0FFFFFFF);
	Write32(OUT, R700_MC_VM_AGP_BOT, 0x0FFFFFFF);

	if (radeon_gpu_mc_idlewait() != B_OK)
		ERROR("%s: Modifying non-idle memory controller!\n", __func__);

	radeon_gpu_mc_resume(&gpuState);

	// disable render control
	Write32(OUT, 0x000300, Read32(OUT, 0x000300) & 0xFFFCFFFF);

	return B_OK;
}


static status_t
radeon_gpu_mc_setup_evergreen()
{
	// HDP initialization
	uint32 i;
	uint32 j;
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		Write32(OUT, (0x2c14 + j), 0x00000000);
		Write32(OUT, (0x2c18 + j), 0x00000000);
		Write32(OUT, (0x2c1c + j), 0x00000000);
		Write32(OUT, (0x2c20 + j), 0x00000000);
		Write32(OUT, (0x2c24 + j), 0x00000000);
	}
	Write32(OUT, EVERGREEN_HDP_REG_COHERENCY_FLUSH_CNTL, 0);

	// idle the memory controller
	struct gpu_state gpuState;
	radeon_gpu_mc_halt(&gpuState);

	if (radeon_gpu_mc_idlewait() != B_OK)
		ERROR("%s: Modifying non-idle memory controller!\n", __func__);

	Write32(OUT, AVIVO_VGA_HDP_CONTROL, AVIVO_VGA_MEMORY_DISABLE);

	// TODO: Memory Controller AGP
	Write32(OUT, EVERGREEN_MC_VM_SYSTEM_APERTURE_LOW_ADDR,
		gInfo->fb.vramStart >> 12);
	Write32(OUT, EVERGREEN_MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
		gInfo->fb.vramEnd >> 12);

	Write32(OUT, EVERGREEN_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR, 0);

	radeon_shared_info &info = *gInfo->shared_info;
	if ((info.chipsetFlags & CHIP_IGP) != 0) {
		// Evergreen IGP Fusion
		uint32 tmp = Read32(OUT, EVERGREEN_MC_FUS_VM_FB_OFFSET)
			& 0x000FFFFF;
		tmp |= ((gInfo->fb.vramEnd >> 20) & 0xF) << 24;
		tmp |= ((gInfo->fb.vramStart >> 20) & 0xF) << 20;
		Write32(OUT, EVERGREEN_MC_FUS_VM_FB_OFFSET, tmp);
	}

	uint32 tmp = ((gInfo->fb.vramEnd >> 24) & 0xFFFF) << 16;
	tmp |= ((gInfo->fb.vramStart >> 24) & 0xFFFF);

	Write32(OUT, EVERGREEN_MC_VM_FB_LOCATION, tmp);
	Write32(OUT, EVERGREEN_HDP_NONSURFACE_BASE, (gInfo->fb.vramStart >> 8));
	Write32(OUT, EVERGREEN_HDP_NONSURFACE_INFO, (2 << 7) | (1 << 30));
	Write32(OUT, EVERGREEN_HDP_NONSURFACE_SIZE, 0x3FFFFFFF);

	// is AGP?
	//	Write32(OUT, EVERGREEN_MC_VM_AGP_TOP, gInfo->fb.gartEnd >> 16);
	//	Write32(OUT, EVERGREEN_MC_VM_AGP_BOT, gInfo->fb.gartStart >> 16);
	//	Write32(OUT, EVERGREEN_MC_VM_AGP_BASE, gInfo->fb.agpBase >> 22);
	// else?
	Write32(OUT, EVERGREEN_MC_VM_AGP_BASE, 0);
	Write32(OUT, EVERGREEN_MC_VM_AGP_TOP, 0x0FFFFFFF);
	Write32(OUT, EVERGREEN_MC_VM_AGP_BOT, 0x0FFFFFFF);

	if (radeon_gpu_mc_idlewait() != B_OK)
		ERROR("%s: Modifying non-idle memory controller!\n", __func__);

	radeon_gpu_mc_resume(&gpuState);

	// disable render control
	Write32(OUT, 0x000300, Read32(OUT, 0x000300) & 0xFFFCFFFF);

	return B_OK;
}


void
radeon_gpu_mc_init()
{
	radeon_shared_info &info = *gInfo->shared_info;

	uint32 fbVMLocationReg;
	if (info.chipsetID >= RADEON_CEDAR) {
		fbVMLocationReg = EVERGREEN_MC_VM_FB_LOCATION;
	} else if (info.chipsetID >= RADEON_RV770) {
		fbVMLocationReg = R700_MC_VM_FB_LOCATION;
	} else {
		fbVMLocationReg = R600_MC_VM_FB_LOCATION;
	}

	if (gInfo->shared_info->frame_buffer_size > 0)
		gInfo->fb.valid = true;

	// This is the card internal location.
	uint64 vramBase = 0;

	if ((info.chipsetFlags & CHIP_IGP) != 0) {
		vramBase = Read32(OUT, fbVMLocationReg) & 0xFFFF;
		vramBase <<= 24;
	}

	gInfo->fb.vramStart = vramBase;
	gInfo->fb.vramSize = (uint64)gInfo->shared_info->frame_buffer_size * 1024;
	gInfo->fb.vramEnd = (vramBase + gInfo->fb.vramSize) - 1;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static void
radeon_vram_pool_report()
{
	radeon_shared_info* info = gInfo->shared_info;
	ERROR("%s: R5 radeon_hd: pool 0x%" B_PRIx64 "+0x%" B_PRIx64
		" gpu base 0x%" B_PRIx64 "\n", __func__,
		info->vram_pool_offset, info->vram_pool_bytes, gInfo->fb.vramStart);
}


status_t
radeon_vram_alloc(uint64 bytes, uint64 alignment, uint64* _offset,
	uint8** _address)
{
	if (_offset == NULL || bytes == 0)
		return B_BAD_VALUE;
	if (alignment == 0 || (alignment & (alignment - 1)) != 0)
		return B_BAD_VALUE;
	if (alignment < 4096)
		alignment = 4096;

	radeon_shared_info* info = gInfo->shared_info;
	Autolock lock(info->accelerant_lock);
	if (!lock.IsLocked())
		return B_ERROR;

	uint64 start = info->vram_pool_offset;
	uint64 end = start + info->vram_pool_bytes;
	uint64 cursor = info->vram_pool_cursor;
	if (cursor < start)
		cursor = start;
	uint64 aligned = (cursor + alignment - 1) & ~(alignment - 1);
	if (aligned < cursor || aligned > end || bytes > end - aligned)
		return B_NO_MEMORY;

	info->vram_pool_cursor = aligned + bytes;
	*_offset = aligned;

	if (_address != NULL) {
		uint8* base = (uint8*)info->frame_buffer;
		if (!radeon_hd_uses_dce(info->chipsetID))
			base -= info->vram_reserved_offset;
		*_address = base + aligned;
	}
	return B_OK;
}
// SPDX-SnippetEnd


status_t
radeon_gpu_mc_setup()
{
	radeon_shared_info &info = *gInfo->shared_info;

	radeon_gpu_mc_init();
		// init video ram ranges for memory controler

	if (gInfo->fb.valid != true) {
		ERROR("%s: Memory Controller init failed.\n", __func__);
		return B_ERROR;
	}

	TRACE("%s: vramStart: 0x%" B_PRIX64 ", vramEnd: 0x%" B_PRIX64 "\n",
		__func__, gInfo->fb.vramStart, gInfo->fb.vramEnd);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	if (!radeon_hd_uses_dce(info.chipsetID)) {
		// Evergreen MC registers are not the Raven display controller.
		// Programming them halts a controller this driver does not own.
		TRACE("%s: R5 radeon_hd: DCN, leave the boot memory aperture alone\n",
			__func__);
		radeon_vram_pool_report();
		return B_OK;
	}
// SPDX-SnippetEnd
	status_t result = B_ERROR;
	if (info.chipsetID >= RADEON_CAYMAN)
		result = radeon_gpu_mc_setup_evergreen();	// also for ni
	else if (info.chipsetID >= RADEON_CEDAR)
		result = radeon_gpu_mc_setup_evergreen();
	else if (info.chipsetID >= RADEON_RV770)
		result = radeon_gpu_mc_setup_r700();
	else if (info.chipsetID >= RADEON_R600)
		result = radeon_gpu_mc_setup_r600();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	if (result == B_OK)
		radeon_vram_pool_report();
// SPDX-SnippetEnd
	return result;
}


status_t
radeon_gpu_ring_setup()
{
	TRACE("%s called\n", __func__);

	// init GFX ring queue
	gInfo->ringQueue[RADEON_QUEUE_TYPE_GFX_INDEX]
		= new RingQueue(1024 * 1024, RADEON_QUEUE_TYPE_GFX_INDEX);

	#if 0
	// init IRQ ring queue (reverse of rendering/cp ring queue)
	gInfo->irqRingQueue
		= new IRQRingQueue(64 * 1024)
	#endif


	return B_OK;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Linux loads Renoir RLC, PFP, ME, and CE through PSP v12. When C2PMSG_81
// shows the secure OS, this driver creates the GPCOM ring and sends
// GFX_CMD_ID_LOAD_IP_FW. PSP v12 on Renoir has no boot-time TMR. Both
// SETUP_TMR forms returned TEE_ERROR_BAD_PARAMETERS from the visible BAR,
// so that command is not sent and it does not block the GFX ring. MEC
// follows amdgpu_ucode_init_single_fw: the body without the jump table is
// GFX_FW_TYPE_CP_MEC, and the table is GFX_FW_TYPE_CP_MEC_ME1. A finished
// non-zero MEC status is logged and the GFX ring still starts. A fence
// timeout still stops the start. gfx_v9_0_load_mec2_fw_bin_support is false
// for GC 9.3.0, so MEC2 is not sent. It does not load the bootloader,
// reroute interrupts, or send a mode-1 reset. A missing sign of life uses
// the direct writes in gfx_v9_0_cp_gfx_load_microcode. The ring setup
// follows gfx_v9_0_cp_gfx_resume, then the clear-state sequence from
// gfx_v9_0_cp_gfx_start, then the scratch packet from
// gfx_v9_0_ring_test_ring. GFX9 always sets use_doorbell. The 64-bit
// write pointer is stored at the poll address and then written with
// WDOORBELL64 at AMDGPU_DOORBELL64_GFX_RING0 (qword 0x8b, dword index
// 0x116, byte 0x458). soc15_common_hw_init sets RCC_DOORBELL_APER_EN
// first. CP_RB0_WPTR is the fallback when that BAR is not mapped. The
// read pointer Linux trusts is the writeback at the fence
// page plus 256. After a PSP load, gfx_v9_0_rlc_stop waits for
// the RLC serdes, then gfx_v9_0_rlc_resume clears clock gating and sets
// RLC_ENABLE_F32. An APU does not enable the GUI idle interrupt. Navi is
// not programmed with these registers. Renoir and Cezanne are not the later
// connected-to-CPU parts: the CP ring base is the framebuffer MC address,
// and the GFXHUB system aperture has to cover that window. The clear-state
// dwords are the AMD MIT table in clearstate_gfx9.h. This file does not
// relicense that table.
#include "clearstate_defs.h"
#include "clearstate_gfx9.h"

static const uint32 kGfx9Seg0 = 0x2000;
static const uint32 kGfx9Seg1 = 0xA000;
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
static const uint32 kScratchReg0 = 0x2040;
static const uint32 kType2Nop = 0x80000000;

struct radeon_gfx9_ring_info {
	uint64	ringOffset;
	uint8*	ringAddress;
	uint32	ringBytes;
	uint32	submitted;
	uint64	fenceOffset;
	uint8*	fenceAddress;
	bool	ready;
};

static radeon_gfx9_ring_info sRadeonGfx9Ring;

// AMDGPU_DOORBELL64_GFX_RING0 is qword 0x8b. gfx_v9_0 stores
// doorbell_index as that value shifted left by 1. The control register
// shifts it by 2 again. WDOORBELL64 indexes a u32 pointer, so the byte
// offset is 0x458.
static const uint32 kGfx9DoorbellIndex = 0x8bu << 1;
static const uint32 kGfx9DoorbellByte = kGfx9DoorbellIndex << 2;
// vega10_ip_offset.h NBIO instance 0, segment 2, is 0xD20.
// nbio_7_0_offset.h puts these registers in that segment.
// Renoir uses nbio_v7_0. The byte address is (base + index) * 4.
// A read of 0xffffffff means this window does not decode the register.
static const uint32 kNbioDoorbellAper = (0xD20u + 0x00C0u) * 4u;
static const uint32 kNbioSdma0Range = (0xD20u + 0x01D0u) * 4u;
static const uint32 kNbioSdma1Range = (0xD20u + 0x01D1u) * 4u;
static const uint32 kNbioIhRange = (0xD20u + 0x01D2u) * 4u;
static const uint32 kNbioMmschRange = (0xD20u + 0x01D3u) * 4u;

static uint8* sGfx9Doorbell;
static area_id sGfx9DoorbellArea = -1;
static uint64 sGfx9DoorbellBytes;


void
radeon_gfx9_doorbell_attach(area_id area, uint64 bytes)
{
	if (sGfx9DoorbellArea >= 0) {
		delete_area(sGfx9DoorbellArea);
		sGfx9DoorbellArea = -1;
		sGfx9Doorbell = NULL;
	}
	sGfx9DoorbellBytes = 0;
	if (area < 0 || bytes < (uint64)kGfx9DoorbellByte + sizeof(uint64)) {
		ERROR("%s: R5 radeon_hd: GFX9 doorbell not mapped\n", __func__);
		return;
	}

	void* address = NULL;
	area_id cloned = clone_area("radeon hd doorbell", &address,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, area);
	if (cloned < 0 || address == NULL) {
		ERROR("%s: R5 radeon_hd: GFX9 doorbell clone failed\n", __func__);
		return;
	}
	sGfx9Doorbell = (uint8*)address;
	sGfx9DoorbellArea = cloned;
	sGfx9DoorbellBytes = bytes;
	ERROR("%s: R5 radeon_hd: GFX9 doorbell bytes 0x%" B_PRIx64 "\n",
		__func__, bytes);
}


void
radeon_gfx9_doorbell_detach()
{
	if (sGfx9DoorbellArea >= 0)
		delete_area(sGfx9DoorbellArea);
	sGfx9Doorbell = NULL;
	sGfx9DoorbellArea = -1;
	sGfx9DoorbellBytes = 0;
}


static uint32
radeon_gfx9_byte(uint32 segment, uint32 index)
{
	return (segment + index) * 4;
}


static bool
radeon_mmio_fits(uint32 byteOffset)
{
	area_info info;
	if (gInfo == NULL || gInfo->regs_area < 0)
		return false;
	if (get_area_info(gInfo->regs_area, &info) != B_OK)
		return false;
	return (uint64)byteOffset + sizeof(uint32) <= info.size;
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


// REMOVE-GFX9-SCRATCH-TEST BEGIN
// Temporary. Delete this function and the two calls in radeon_gfx9_prepare_ring,
// and submit radeon_gfx9_fill_start again. The ring is the opening of
// gfx_v9_0_cp_gfx_start, preamble and context control, then the three dwords
// of gfx_v9_0_ring_test_ring. The 212 context registers are not included.
// One doorbell publishes that write pointer. It is not written again.
static uint32
radeon_gfx9_fill_scratch_test(uint32* ring, uint32 capacity)
{
	uint32 scratchReg = (kGfx9Seg1 + kScratchReg0) - kUconfigRegStart;
	if (scratchReg >= 0x400u)
		return 0;
	uint32 n = 0;
	if (!radeon_gfx9_put(ring, capacity, &n,
			radeon_packet3(kPacket3Preamble, 0))
		|| !radeon_gfx9_put(ring, capacity, &n, kPreambleBegin)
		|| !radeon_gfx9_put(ring, capacity, &n,
			radeon_packet3(kPacket3ContextControl, 1))
		|| !radeon_gfx9_put(ring, capacity, &n, 0x80000000)
		|| !radeon_gfx9_put(ring, capacity, &n, 0x80000000)
		|| !radeon_gfx9_put(ring, capacity, &n,
			radeon_packet3(kPacket3SetUconfig, 1))
		|| !radeon_gfx9_put(ring, capacity, &n, scratchReg)
		|| !radeon_gfx9_put(ring, capacity, &n, 0xDEADBEEF))
		return 0;
	if (n != 8)
		return 0;
	return n;
}
// REMOVE-GFX9-SCRATCH-TEST END


static void
radeon_gfx9_prepare_ring()
{
	const uint32 kFenceBytes = 4096;

	memset(&sRadeonGfx9Ring, 0, sizeof(sRadeonGfx9Ring));

	// REMOVE-GFX9-SCRATCH-TEST BEGIN
	// Counted so radeon_gfx9_fill_start stays compiled. Those dwords are
	// not written into the ring.
	(void)radeon_gfx9_fill_start(NULL, 0xffffffffu);
	uint32 submitted = radeon_gfx9_fill_scratch_test(NULL, 0xffffffffu);
	// REMOVE-GFX9-SCRATCH-TEST END
	uint32 ringBytes = radeon_gfx9_ring_bytes(submitted);
	if (submitted == 0 || ringBytes == 0 || radeon_gfx9_rb_cntl(ringBytes) == 0
		|| submitted >= ringBytes / sizeof(uint32)) {
		ERROR("%s: R5 radeon_hd: GFX9 clear state refused\n", __func__);
		return;
	}

	uint64 ringOffset = 0;
	uint8* ringAddress = NULL;
	status_t result = radeon_vram_alloc(ringBytes, ringBytes, &ringOffset,
		&ringAddress);
	if (result != B_OK || ringAddress == NULL) {
		ERROR("%s: R5 radeon_hd: GFX9 ring alloc failed\n", __func__);
		return;
	}

	uint64 fenceOffset = 0;
	uint8* fenceAddress = NULL;
	result = radeon_vram_alloc(kFenceBytes, kFenceBytes, &fenceOffset,
		&fenceAddress);
	if (result != B_OK || fenceAddress == NULL) {
		ERROR("%s: R5 radeon_hd: GFX9 fence alloc failed\n", __func__);
		return;
	}

	uint32* ring = (uint32*)ringAddress;
	uint32 dwords = ringBytes / sizeof(uint32);
	for (uint32 i = 0; i < dwords; i++)
		ring[i] = kType2Nop;
	// REMOVE-GFX9-SCRATCH-TEST BEGIN
	if (radeon_gfx9_fill_scratch_test(ring, dwords) != submitted) {
	// REMOVE-GFX9-SCRATCH-TEST END
		ERROR("%s: R5 radeon_hd: GFX9 clear state refused\n", __func__);
		return;
	}

	volatile uint32* fence = (volatile uint32*)fenceAddress;
	*fence = 0xCAFEDEAD;
	uint32 fenceRead = *fence;
	uint32 headerRead = ring[0];

	sRadeonGfx9Ring.ringOffset = ringOffset;
	sRadeonGfx9Ring.ringAddress = ringAddress;
	sRadeonGfx9Ring.ringBytes = ringBytes;
	sRadeonGfx9Ring.submitted = submitted;
	sRadeonGfx9Ring.fenceOffset = fenceOffset;
	sRadeonGfx9Ring.fenceAddress = fenceAddress;
	sRadeonGfx9Ring.ready = true;

	ERROR("%s: R5 radeon_hd: GFX9 ring 0x%" B_PRIx64 "+0x%" B_PRIx32
		" dwords %" B_PRIu32 " packet 0x%" B_PRIx32 " fence 0x%" B_PRIx64
		" read 0x%" B_PRIx32 "\n", __func__, ringOffset, ringBytes, submitted,
		headerRead, fenceOffset, fenceRead);
	// REMOVE-GFX9-SCRATCH-TEST BEGIN
	ERROR("%s: R5 radeon_hd: GFX9 preamble scratch test\n", __func__);
	// REMOVE-GFX9-SCRATCH-TEST END
}


static uint8*
radeon_read_named_firmware(const char* fileName, size_t* _bytes)
{
	char path[B_PATH_NAME_LENGTH];
	if (find_directory(B_SYSTEM_NONPACKAGED_DATA_DIRECTORY, -1, false, path,
			sizeof(path)) != B_OK)
		return NULL;

	size_t used = strlen(path);
	int wrote = snprintf(path + used, sizeof(path) - used,
		"/firmware/amdgpu/%s", fileName);
	if (wrote < 0 || (size_t)wrote >= sizeof(path) - used)
		return NULL;

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		ERROR("%s: R5 radeon_hd: GFX9 firmware %s missing\n", __func__,
			fileName);
		return NULL;
	}

	// renoir_mec.bin is 268224 bytes. A fixed 256 KiB buffer reported it
	// missing and the ring was never submitted.
	struct stat info;
	const off_t kMaxFirmware = 1024 * 1024;
	if (fstat(fd, &info) != 0) {
		ERROR("%s: R5 radeon_hd: GFX9 firmware %s size unread\n", __func__,
			fileName);
		close(fd);
		return NULL;
	}
	if (info.st_size <= 0 || info.st_size > kMaxFirmware) {
		ERROR("%s: R5 radeon_hd: GFX9 firmware %s refused, size %lld\n",
			__func__, fileName, (long long)info.st_size);
		close(fd);
		return NULL;
	}

	size_t capacity = (size_t)info.st_size;
	uint8* blob = (uint8*)malloc(capacity);
	if (blob == NULL) {
		close(fd);
		return NULL;
	}
	size_t filled = 0;
	while (filled < capacity) {
		ssize_t got = read(fd, blob + filled, capacity - filled);
		if (got < 0) {
			free(blob);
			close(fd);
			return NULL;
		}
		if (got == 0)
			break;
		filled += (size_t)got;
	}
	close(fd);
	if (filled != capacity) {
		free(blob);
		ERROR("%s: R5 radeon_hd: GFX9 firmware %s short read\n", __func__,
			fileName);
		return NULL;
	}
	*_bytes = filled;
	return blob;
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
	uint16			headerMajor;
	uint16			headerMinor;
	// gfx_firmware_header_v1_0 jt_offset and jt_size, in bytes from the
	// ucode array. Zero when the header is not 1.0 or the table is empty.
	uint32			jtOffset;
	uint32			jtBytes;
};


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
			if (jtOk) {
				out->blob = blob;
				out->ucode = blob + offset;
				out->ucodeBytes = ucodeBytes;
				out->version = version;
				out->headerMajor = headerMajor;
				out->headerMinor = headerMinor;
				out->jtOffset = jtOffset;
				out->jtBytes = jtBytes;
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
	status_t result = radeon_vram_alloc(padded, 4096, &item->offset,
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
	uint16 pciID = gInfo->shared_info->pciID;
	// amdgpu_device_init_apu_flags: 1636 and 164c are AMD_APU_IS_RENOIR.
	// 15e7 Barcelo and 1638 Cezanne are AMD_APU_IS_GREEN_SARDINE.
	// The PFP, ME, and CE blobs differ. RLC and MEC are the same bytes.
	if (pciID == 0x1636 || pciID == 0x164c)
		return "renoir";
	return "green_sardine";
}


static status_t
radeon_psp_load_engines(uint32 frameBase)
{
	const char* stem = radeon_gfx9_fw_stem();
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
	// CP_MEC_ME1 5, RLC_G 8. jtType 0 sends the whole ucode. A nonzero
	// jtType splits a suffix table the way amdgpu_ucode_init_single_fw
	// does. gfx_v9_0_load_mec2_fw_bin_support is false for GC 9.3.0, so
	// this path does not send CP_MEC_ME2.
	struct {
		const char*	name;
		uint32		type;
		uint32		jtType;
		bool		required;
	} files[5] = {
		{ rlc, 8, 0, false },
		{ pfp, 2, 0, true },
		{ ce, 3, 0, true },
		{ me, 1, 0, true },
		{ mec, 4, 5, true },
	};

	ERROR("%s: R5 radeon_hd: PSP MEC2 not loaded on GC 9.3\n", __func__);
	// Both SETUP_TMR forms returned 0xffff0006 from the visible BAR:
	// the GPU address and size, and virt_phy_addr with the CPU physical
	// address of the same page. Linux can put the 4 MiB region in stolen
	// memory above that BAR. This pool cannot. GFX does not wait for it.
	ERROR("%s: R5 radeon_hd: PSP SETUP_TMR skipped\n", __func__);

	radeon_psp_item items[8];
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

		if (files[i].jtType == 0) {
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
			&& count + 2 <= 8;
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
	status_t result = radeon_vram_alloc(kPspRingBytes, kPspRingBytes,
		&ringOffset, &ringAddress);
	if (result != B_OK || ringAddress == NULL)
		return result == B_OK ? B_ERROR : result;
	memset(ringAddress, 0, kPspRingBytes);

	uint64 cmdOffset = 0;
	uint8* cmdAddress = NULL;
	result = radeon_vram_alloc(4096, 4096, &cmdOffset, &cmdAddress);
	if (result != B_OK || cmdAddress == NULL)
		return result == B_OK ? B_ERROR : result;
	memset(cmdAddress, 0, 4096);

	uint64 fenceOffset = 0;
	uint8* fenceAddress = NULL;
	result = radeon_vram_alloc(4096, 4096, &fenceOffset, &fenceAddress);
	if (result != B_OK || fenceAddress == NULL)
		return result == B_OK ? B_ERROR : result;
	memset(fenceAddress, 0, 4096);
	radeon_visible_barrier();

	uint64 ringGpu = ((uint64)frameBase << 24) + ringOffset;
	uint64 cmdGpu = ((uint64)frameBase << 24) + cmdOffset;
	uint64 fenceGpu = ((uint64)frameBase << 24) + fenceOffset;
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

	for (uint32 i = 0; i < count; i++) {
		uint32 wptr = Read32(OUT, writePtr);
		if (wptr >= (kPspRingBytes / 4) || (wptr % 16) != 0) {
			ERROR("%s: R5 radeon_hd: PSP wptr 0x%" B_PRIx32 "\n",
				__func__, wptr);
			return B_ERROR;
		}

		uint64 fwGpu = ((uint64)frameBase << 24) + items[i].offset;
		memset(cmdAddress, 0, kPspCmdBytes);
		radeon_psp_store32(cmdAddress + 0, kPspCmdBytes);
		radeon_psp_store32(cmdAddress + 4, 1);
		radeon_psp_store32(cmdAddress + 8, 6);
		radeon_psp_store32(cmdAddress + 28, (uint32)fwGpu);
		radeon_psp_store32(cmdAddress + 32, (uint32)(fwGpu >> 32));
		radeon_psp_store32(cmdAddress + 36, items[i].bytes);
		radeon_psp_store32(cmdAddress + 40, items[i].type);

		uint8* frame = ringAddress + (wptr * 4);
		memset(frame, 0, 64);
		// psp_ring_cmd_submit leaves cmd_buf_size at 0.
		radeon_psp_store32(frame + 0, (uint32)cmdGpu);
		radeon_psp_store32(frame + 4, (uint32)(cmdGpu >> 32));
		radeon_psp_store32(frame + 12, (uint32)fenceGpu);
		radeon_psp_store32(frame + 16, (uint32)(fenceGpu >> 32));
		uint32 fenceValue = i + 1;
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
		uint32 status = radeon_le32(cmdAddress + 864);
		ERROR("%s: R5 radeon_hd: PSP %s type %" B_PRIu32
			" bytes %" B_PRIu32 " status 0x%" B_PRIx32
			" fence 0x%" B_PRIx32 "\n",
			__func__, items[i].name, items[i].type, items[i].bytes,
			status, fenceSeen);
		if (!landed)
			return B_TIMED_OUT;
		if (status != 0) {
			// RLC, PFP, CE, and ME stay required. MEC is the
			// compute engine; its refusal must not skip the ring.
			if (!items[i].optional)
				return B_ERROR;
			ERROR("%s: R5 radeon_hd: MEC left unloaded type %"
				B_PRIu32 " status 0x%" B_PRIx32 "\n",
				__func__, items[i].type, status);
		}
	}
	return B_OK;
}


static bool radeon_gfx9_program_csib(uint32 frameBase);


static bool
radeon_gfx9_rlc_start(uint32 frameBase, bool loadUcode, uint32* _before,
	uint32* _clock, uint32* _after, uint32* _version)
{
	const uint32 cntl = radeon_gfx9_byte(kGfx9Seg1, 0x4c00);
	const uint32 clock = radeon_gfx9_byte(kGfx9Seg1, 0x4c49);
	const uint32 version = radeon_gfx9_byte(kGfx9Seg1, 0x4c69);
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
	const uint32 cuBusy = radeon_gfx9_byte(kGfx9Seg1, 0x4c61);
	const uint32 noncuBusy = radeon_gfx9_byte(kGfx9Seg1, 0x4c62);
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
	if (!radeon_gfx9_program_csib(frameBase)) {
		Write32(OUT, cntl, saved);
		Write32(OUT, clock, savedClock);
		ERROR("%s: R5 radeon_hd: GFX9 clear-state buffer refused\n",
			__func__);
		return false;
	}
	if (loadUcode) {
		char rlc[32];
		snprintf(rlc, sizeof(rlc), "%s_rlc.bin", radeon_gfx9_fw_stem());
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
	Write32(OUT, cntl, (saved & ~1u) | 1u);
	snooze(50);
	uint32 after = Read32(OUT, cntl);
	uint32 seenVersion = Read32(OUT, version);
	*_after = after;
	*_version = seenVersion;
	ERROR("%s: R5 radeon_hd: RLC_CNTL 0x%" B_PRIx32 " -> 0x%" B_PRIx32
		" RLC_GPM_GENERAL_6 0x%" B_PRIx32 "\n", __func__, saved, after,
		seenVersion);
	if ((after & 1u) == 0) {
		Write32(OUT, cntl, saved);
		Write32(OUT, clock, savedClock);
		ERROR("%s: R5 radeon_hd: RLC enable did not stick\n", __func__);
		return false;
	}
	return true;
}


static void
radeon_gfx9_publish(bool psp, bool rlc, uint32 rlcBefore, uint32 rlcAfter,
	uint32 rlcVersion, uint32 scratch, bool packet, bool programmed,
	uint32 dwords, uint32 rptr, uint32 rptrBefore)
{
	char path[B_PATH_NAME_LENGTH];
	if (find_directory(B_SYSTEM_CACHE_DIRECTORY, -1, true, path,
			sizeof(path)) != B_OK)
		return;

	size_t used = strlen(path);
	int wrote = snprintf(path + used, sizeof(path) - used, "/radeon-gfx.txt");
	if (wrote < 0 || (size_t)wrote >= sizeof(path) - used)
		return;

	FILE* file = fopen(path, "w");
	if (file == NULL)
		return;
	fprintf(file, "device %04x\n", gInfo->shared_info->pciID);
	fprintf(file, "psp %s\n", psp ? "yes" : "no");
	fprintf(file, "rlc %s\n", rlc ? "yes" : "no");
	fprintf(file, "rlc_before %08x\n", rlcBefore);
	fprintf(file, "rlc_after %08x\n", rlcAfter);
	fprintf(file, "rlc_version %08x\n", rlcVersion);
	fprintf(file, "scratch %08x\n", scratch);
	fprintf(file, "packet %s\n", packet ? "yes" : "no");
	if (programmed) {
		fprintf(file, "dwords %u\n", dwords);
		fprintf(file, "rptr_before %08x\n", rptrBefore);
		fprintf(file, "rptr %08x\n", rptr);
		fprintf(file, "wptr %08x\n", dwords);
	}
	fclose(file);
}



static void
radeon_gfx9_flush_visible(uint8* address, uint32 bytes)
{
	volatile uint8* bytesView = address;
	for (uint32 i = 0; i < bytes; i += 64)
		(void)bytesView[i];
}


// gfx_v9_0_init_golden_registers for GC 9.3 writes this list and returns.
// GCEA_PROBE_MAP is in that list and is not a GC register in gc_9_0, so it
// is not programmed here. soc15_program_register_sequence uses the and mask.
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
};

static_assert(sizeof(kGfx9Golden) / sizeof(kGfx9Golden[0]) == 11,
	"GFX9 golden save slot count");


struct radeon_gfx9_saved {
	bool	armed;
	uint32	fbBase;
	uint32	fbTop;
	uint32	aperLow;
	uint32	aperHigh;
	uint32	defLo;
	uint32	defHi;
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
	uint32	golden[11];
};

static radeon_gfx9_saved sRadeonGfx9Saved;


static void
radeon_gfx9_saved_restore()
{
	if (!sRadeonGfx9Saved.armed)
		return;
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0980), sRadeonGfx9Saved.fbBase);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0981), sRadeonGfx9Saved.fbTop);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0985), sRadeonGfx9Saved.aperLow);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0986), sRadeonGfx9Saved.aperHigh);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x096c), sRadeonGfx9Saved.defLo);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x096d), sRadeonGfx9Saved.defHi);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0857), sRadeonGfx9Saved.l2);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0000), sRadeonGfx9Saved.grbm);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c48), sRadeonGfx9Saved.mgcg);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c06), sRadeonGfx9Saved.rlcMem);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x1079), sRadeonGfx9Saved.cpMem);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c43), sRadeonGfx9Saved.pg);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4cc5), sRadeonGfx9Saved.cgcg3d);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4ca2), sRadeonGfx9Saved.csibLo);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4ca3), sRadeonGfx9Saved.csibHi);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4ca4), sRadeonGfx9Saved.csibLen);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c80), sRadeonGfx9Saved.srm);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x008d), sRadeonGfx9Saved.mec);
	for (uint32 i = 0; i < 11; i++) {
		Write32(OUT, radeon_gfx9_byte(kGfx9Golden[i].segment,
			kGfx9Golden[i].index), sRadeonGfx9Saved.golden[i]);
	}
	sRadeonGfx9Saved.armed = false;
}


// gfxhub_v1_0_init_system_aperture_regs for Renoir and Green Sardine. The
// system aperture is in 256 KiB pages. Renoir faults and hangs when a VRAM
// address is above MC_VM_SYSTEM_APERTURE_HIGH_ADDR, so the high page is one
// past the end of the framebuffer. This only widens a window. It does not
// install the MI300 page table and it does not move the display aperture.
static bool
radeon_gfx9_prepare_hw(uint32 frameBase)
{
	const uint32 highest = radeon_gfx9_byte(kGfx9Seg1, 0x4cc5);
	uint64 frame = (uint64)frameBase << 24;
	uint64 fbEnd = frame + gInfo->fb.vramSize - 1;
	if (!radeon_mmio_fits(highest) || gInfo->fb.vramSize < 0x1000
		|| fbEnd < frame || (fbEnd >> 18) == 0xffffffffull)
		return false;

	// gfxhub_v1_0 programs MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR from
	// mem_scratch. On this APU vram_mc2pa is the MC address itself.
	// The fence page stays out of that redirect.
	uint64 scratchOffset = 0;
	uint8* scratchAddress = NULL;
	if (radeon_vram_alloc(4096, 4096, &scratchOffset, &scratchAddress) != B_OK
		|| scratchAddress == NULL)
		return false;
	memset(scratchAddress, 0, 4096);
	radeon_visible_barrier();

	memset(&sRadeonGfx9Saved, 0, sizeof(sRadeonGfx9Saved));
	sRadeonGfx9Saved.fbBase = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0980));
	sRadeonGfx9Saved.fbTop = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0981));
	sRadeonGfx9Saved.aperLow = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0985));
	sRadeonGfx9Saved.aperHigh = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0986));
	sRadeonGfx9Saved.defLo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x096c));
	sRadeonGfx9Saved.defHi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x096d));
	sRadeonGfx9Saved.l2 = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0857));
	sRadeonGfx9Saved.grbm = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0000));
	sRadeonGfx9Saved.mgcg = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c48));
	sRadeonGfx9Saved.rlcMem = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c06));
	sRadeonGfx9Saved.cpMem = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x1079));
	sRadeonGfx9Saved.pg = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c43));
	sRadeonGfx9Saved.cgcg3d = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4cc5));
	sRadeonGfx9Saved.csibLo = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4ca2));
	sRadeonGfx9Saved.csibHi = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4ca3));
	sRadeonGfx9Saved.csibLen = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4ca4));
	sRadeonGfx9Saved.srm = Read32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c80));
	sRadeonGfx9Saved.mec = Read32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x008d));
	for (uint32 i = 0; i < 11; i++) {
		sRadeonGfx9Saved.golden[i] = Read32(OUT,
			radeon_gfx9_byte(kGfx9Golden[i].segment, kGfx9Golden[i].index));
	}
	sRadeonGfx9Saved.armed = true;

	uint32 base = sRadeonGfx9Saved.fbBase & 0x00ffffffu;
	uint32 top = sRadeonGfx9Saved.fbTop & 0x00ffffffu;
	uint32 topNeed = (uint32)(fbEnd >> 24);
	bool fbDisabled = base > top;
	if (fbDisabled || base > frameBase)
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0980), frameBase);
	if (fbDisabled || top < topNeed)
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0981), topNeed);

	uint32 lowNeed = (uint32)(frame >> 18);
	uint32 highNeed = (uint32)(fbEnd >> 18) + 1u;
	if (sRadeonGfx9Saved.aperLow > lowNeed)
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0985), lowNeed);
	if (sRadeonGfx9Saved.aperHigh < highNeed)
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0986), highNeed);

	uint64 scratchGpu = frame + scratchOffset;
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x096c),
		(uint32)(scratchGpu >> 12));
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x096d),
		(uint32)(scratchGpu >> 44));
	// Not an XGMI-to-CPU part: the walker does not force physical requests.
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0857),
		sRadeonGfx9Saved.l2 & ~((1u << 6) | (1u << 7)));

	uint32 grbm = (sRadeonGfx9Saved.grbm & ~0xffu) | 0xffu;
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x0000), grbm);
	// gfx_v9_0_update_gfx_clock_gating holds these clocks when gating is off.
	uint32 mgcg = sRadeonGfx9Saved.mgcg | (1u << 0) | (1u << 1) | (1u << 2)
		| (1u << 5) | (1u << 6);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c48), mgcg);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c06),
		sRadeonGfx9Saved.rlcMem & ~1u);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x1079),
		sRadeonGfx9Saved.cpMem & ~1u);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c43),
		sRadeonGfx9Saved.pg & ~((1u << 0) | (1u << 2) | (1u << 3) | (1u << 4)));
	// gfx_v9_0_update_3d_clock_gating clears CGCG_EN and CGLS_EN only.
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4cc5),
		sRadeonGfx9Saved.cgcg3d & ~((1u << 0) | (1u << 1)));
	// No compute ring is programmed. Leave MEC ME1 and ME2 halted.
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg0, 0x008d),
		sRadeonGfx9Saved.mec | (1u << 28) | (1u << 30));

	for (uint32 i = 0; i < 11; i++) {
		uint32 reg = radeon_gfx9_byte(kGfx9Golden[i].segment,
			kGfx9Golden[i].index);
		uint32 value = kGfx9Golden[i].orMask;
		if (kGfx9Golden[i].andMask != 0xffffffffu) {
			value = (sRadeonGfx9Saved.golden[i] & ~kGfx9Golden[i].andMask)
				| (kGfx9Golden[i].orMask & kGfx9Golden[i].andMask);
		}
		Write32(OUT, reg, value);
	}

	ERROR("%s: R5 radeon_hd: GFX9 aperture low 0x%" B_PRIx32
		" -> 0x%" B_PRIx32 " high 0x%" B_PRIx32 " -> 0x%" B_PRIx32 "\n",
		__func__, sRadeonGfx9Saved.aperLow, lowNeed, sRadeonGfx9Saved.aperHigh,
		highNeed);
	return true;
}


static bool
radeon_gfx9_program_csib(uint32 frameBase)
{
	uint32 dwords = radeon_gfx9_fill(NULL, 0xffffffffu, true);
	if (dwords == 0 || dwords + 10 != kGfx9StartDwords)
		return false;

	uint64 offset = 0;
	uint8* address = NULL;
	status_t result = radeon_vram_alloc(4096, 4096, &offset, &address);
	if (result != B_OK || address == NULL)
		return false;
	memset(address, 0, 4096);
	if (radeon_gfx9_fill((uint32*)address, 4096 / sizeof(uint32), true)
		!= dwords)
		return false;
	radeon_gfx9_flush_visible(address, 4096);
	radeon_visible_barrier();

	uint64 gpu = ((uint64)frameBase << 24) + offset;
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4ca3), (uint32)(gpu >> 32));
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4ca2), (uint32)gpu & ~3u);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4ca4), dwords);
	Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c80), sRadeonGfx9Saved.srm | 1u);
	ERROR("%s: R5 radeon_hd: GFX9 CSIB 0x%" B_PRIx64 " dwords %" B_PRIu32
		"\n", __func__, gpu, dwords);
	return true;
}


static void
radeon_gfx9_note_doorbell(bool rang, uint32 control, uint64 poll,
	uint32 rptrWb, uint32 rptrAtBell, uint32 rptrWbAtBell,
	uint32 aperBefore, uint32 aperAfter, uint32 sdma0, uint32 sdma1,
	uint32 ihRange, uint32 mmsch)
{
	char path[B_PATH_NAME_LENGTH];
	if (find_directory(B_SYSTEM_CACHE_DIRECTORY, -1, true, path,
			sizeof(path)) != B_OK)
		return;

	size_t used = strlen(path);
	int wrote = snprintf(path + used, sizeof(path) - used, "/radeon-gfx.txt");
	if (wrote < 0 || (size_t)wrote >= sizeof(path) - used)
		return;

	FILE* file = fopen(path, "a");
	if (file == NULL)
		return;
	fprintf(file, "doorbell %s\n", rang ? "yes" : "no");
	fprintf(file, "doorbell_ctl %08x\n", control);
	fprintf(file, "poll %016" B_PRIx64 "\n", poll);
	fprintf(file, "rptr_at_bell %08x\n", rptrAtBell);
	fprintf(file, "rptr_wb_at_bell %08x\n", rptrWbAtBell);
	fprintf(file, "rptr_wb %08x\n", rptrWb);
	fprintf(file, "aperture_before %08x\n", aperBefore);
	fprintf(file, "aperture_after %08x\n", aperAfter);
	fprintf(file, "sdma0_range %08x\n", sdma0);
	fprintf(file, "sdma1_range %08x\n", sdma1);
	fprintf(file, "ih_range %08x\n", ihRange);
	fprintf(file, "mmsch_range %08x\n", mmsch);
	fclose(file);
}



static void
radeon_gfx9_cp_start()
{
	if (!sRadeonGfx9Ring.ready)
		return;

	uint32 cntl = radeon_gfx9_rb_cntl(sRadeonGfx9Ring.ringBytes);
	if (cntl == 0 || sRadeonGfx9Ring.submitted == 0
		|| sRadeonGfx9Ring.submitted
			>= sRadeonGfx9Ring.ringBytes / sizeof(uint32)) {
		ERROR("%s: R5 radeon_hd: GFX9 ring size refused\n", __func__);
		return;
	}

	uint32 highest = radeon_gfx9_byte(kGfx9Seg1, 0x5819);
	if (!radeon_mmio_fits(highest)
		|| !radeon_mmio_fits(radeon_gfx9_byte(kGfx9Seg1, 0x2040))) {
		ERROR("%s: R5 radeon_hd: GFX9 mmio does not cover CP ucode\n",
			__func__);
		return;
	}

	const uint32 meCntl = radeon_gfx9_byte(kGfx9Seg0, 0x01b6);
	const uint32 fbOffset = radeon_gfx9_byte(kGfx9Seg0, 0x096b);
	const uint32 rbCntl = radeon_gfx9_byte(kGfx9Seg0, 0x1041);
	const uint32 rbRptrAddr = radeon_gfx9_byte(kGfx9Seg0, 0x1043);
	const uint32 rbRptrAddrHi = radeon_gfx9_byte(kGfx9Seg0, 0x1044);
	const uint32 rbPollLo = radeon_gfx9_byte(kGfx9Seg0, 0x1046);
	const uint32 rbPollHi = radeon_gfx9_byte(kGfx9Seg0, 0x1047);
	const uint32 deviceId = radeon_gfx9_byte(kGfx9Seg0, 0x104b);
	const uint32 rbVmid = radeon_gfx9_byte(kGfx9Seg0, 0x1051);
	const uint32 rbWptr = radeon_gfx9_byte(kGfx9Seg0, 0x1054);
	const uint32 rbWptrHi = radeon_gfx9_byte(kGfx9Seg0, 0x1055);
	const uint32 rbDoorbell = radeon_gfx9_byte(kGfx9Seg0, 0x1059);
	const uint32 rbRangeLo = radeon_gfx9_byte(kGfx9Seg0, 0x105a);
	const uint32 rbRangeHi = radeon_gfx9_byte(kGfx9Seg0, 0x105b);
	const uint32 maxContext = radeon_gfx9_byte(kGfx9Seg0, 0x10ae);
	const uint32 rbBase = radeon_gfx9_byte(kGfx9Seg0, 0x1040);
	const uint32 rbBaseHi = radeon_gfx9_byte(kGfx9Seg0, 0x10b1);
	const uint32 rbRptr = radeon_gfx9_byte(kGfx9Seg0, 0x01c0);
	const uint32 rbRptrWr = radeon_gfx9_byte(kGfx9Seg0, 0x1042);
	const uint32 rbDelay = radeon_gfx9_byte(kGfx9Seg0, 0x01c1);
	const uint32 scratch = radeon_gfx9_byte(kGfx9Seg1, 0x2040);
	const uint32 haltMask = radeon_gfx9_halt_mask();

	uint32 savedMe = Read32(OUT, meCntl);
	ERROR("%s: R5 radeon_hd: GFX9 CP_ME_CNTL 0x%" B_PRIx32
		" before load\n", __func__, savedMe);

	Write32(OUT, meCntl, savedMe | haltMask);
	snooze(1000);

	uint32 fb = Read32(OUT, fbOffset);
	uint64 ringGpu = ((uint64)fb << 24) + sRadeonGfx9Ring.ringOffset;
	ERROR("%s: R5 radeon_hd: GFX9 MC_VM_FB_OFFSET 0x%" B_PRIx32
		" ring gpu 0x%" B_PRIx64 "\n", __func__, fb, ringGpu);
	if (fb == 0xffffffff || (ringGpu & 0xff) != 0) {
		Write32(OUT, meCntl, savedMe);
		ERROR("%s: R5 radeon_hd: GFX9 ring base refused\n", __func__);
		return;
	}
	if (!radeon_gfx9_prepare_hw(fb)) {
		Write32(OUT, meCntl, savedMe);
		radeon_gfx9_saved_restore();
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

	status_t loaded = B_ERROR;
	if (pspOwnsLoad)
		loaded = radeon_psp_load_engines(fb);
	else {
		char pfp[32];
		char ce[32];
		char me[32];
		const char* stem = radeon_gfx9_fw_stem();
		snprintf(pfp, sizeof(pfp), "%s_pfp.bin", stem);
		snprintf(ce, sizeof(ce), "%s_ce.bin", stem);
		snprintf(me, sizeof(me), "%s_me.bin", stem);
		loaded = radeon_gfx9_load_engine(pfp, 0x5814, 0x5815, 0);
		if (loaded == B_OK)
			loaded = radeon_gfx9_load_engine(ce, 0x5818, 0x5819, 0);
		if (loaded == B_OK)
			loaded = radeon_gfx9_load_engine(me, 0x5816, 0x5817, 0);
	}
	if (loaded != B_OK) {
		Write32(OUT, meCntl, savedMe);
		radeon_gfx9_saved_restore();
		radeon_gfx9_publish(pspOwnsLoad, false, 0, 0, 0, 0, false, false, 0,
			0, 0);
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
	rlcStarted = radeon_gfx9_rlc_start(fb, !pspOwnsLoad, &rlcBefore, &rlcClock,
		&rlcAfter, &rlcVersion);
	if (!rlcStarted) {
		Write32(OUT, meCntl, savedMe);
		radeon_gfx9_saved_restore();
		radeon_gfx9_publish(pspOwnsLoad, false, rlcBefore, rlcAfter,
			rlcVersion, 0, false, false, 0, 0, 0);
		return;
	}

	Write32(OUT, scratch, 0xCAFEDEAD);
	uint32 scratchRead = Read32(OUT, scratch);
	if (scratchRead != 0xCAFEDEAD) {
		Write32(OUT, meCntl, savedMe);
		if (rlcStarted) {
			Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c00), rlcBefore);
			Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c49), rlcClock);
		}
		radeon_gfx9_saved_restore();
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
	*(volatile uint32*)(sRadeonGfx9Ring.fenceAddress + 256) = 0;
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
	bool ringDoorbell = sGfx9Doorbell != NULL
		&& sGfx9DoorbellBytes >= (uint64)kGfx9DoorbellByte + sizeof(uint64);
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
	radeon_visible_barrier();
	uint64 pollRead = *(volatile uint64*)(sRadeonGfx9Ring.fenceAddress + 512);
	uint32 doorbellNow = Read32(OUT, rbDoorbell);
	if (ringDoorbell) {
		*(volatile uint64*)(sGfx9Doorbell + kGfx9DoorbellByte) = wptr;
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
	if (scratchNow == 0xDEADBEEF) {
		radeon_gfx9_publish(pspOwnsLoad, rlcStarted, rlcBefore, rlcAfter,
			rlcVersion, scratchNow, true, true, sRadeonGfx9Ring.submitted,
			rptrNow, rptrBefore);
		radeon_gfx9_note_doorbell(ringDoorbell, doorbellNow, pollRead, rptrWb,
			rptrAtBell, rptrWbAtBell, aperBefore, aperAfter, sdma0Range,
			sdma1Range, ihRange, mmschRange);
		ERROR("%s: R5 radeon_hd: GFX9 scratch 0x%" B_PRIx32
			" rptr 0x%" B_PRIx32 " dwords %" B_PRIu32 ", fence landed\n",
			__func__, scratchNow, rptrNow, sRadeonGfx9Ring.submitted);
		return;
	}

	Write32(OUT, meCntl, savedMe | haltMask);
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
	if (rlcStarted) {
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c00), rlcBefore);
		Write32(OUT, radeon_gfx9_byte(kGfx9Seg1, 0x4c49), rlcClock);
	}
	radeon_gfx9_saved_restore();
	radeon_gfx9_publish(pspOwnsLoad, rlcStarted, rlcBefore, rlcAfter,
		rlcVersion, scratchNow, false, true, sRadeonGfx9Ring.submitted,
		rptrNow, rptrBefore);
	radeon_gfx9_note_doorbell(ringDoorbell, doorbellNow, pollRead, rptrWb,
		rptrAtBell, rptrWbAtBell, aperBefore, aperAfter, sdma0Range,
		sdma1Range, ihRange, mmschRange);
	ERROR("%s: R5 radeon_hd: GFX9 scratch stayed 0x%" B_PRIx32
		" rptr 0x%" B_PRIx32 " dwords %" B_PRIu32 ", ring restored\n",
		__func__, scratchNow, rptrNow, sRadeonGfx9Ring.submitted);
}
// SPDX-SnippetEnd


status_t
radeon_gpu_ring_boot(uint32 ringType)
{
	TRACE("%s called\n", __func__);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	if (!radeon_hd_uses_dce(gInfo->shared_info->chipsetID)) {
		uint16 chipsetID = gInfo->shared_info->chipsetID;
		if (ringType == RADEON_QUEUE_TYPE_GFX_INDEX
			&& chipsetID >= RADEON_RAVEN && chipsetID <= RADEON_RENOIR) {
			radeon_gfx9_prepare_ring();
			uint32 pciID = gInfo->shared_info->pciID;
			if (pciID == 0x15e7 || pciID == 0x1636 || pciID == 0x1638)
				radeon_gfx9_cp_start();
			else if (pciID == 0x164c) {
				ERROR("%s: R5 radeon_hd: Lucienne firmware stays unloaded\n",
					__func__);
			} else if (sRadeonGfx9Ring.ready) {
				ERROR("%s: R5 radeon_hd: GFX9 CP not touched, "
					"fence not submitted\n", __func__);
			}
		} else {
			ERROR("%s: R5 radeon_hd: GFX9 ring stays off\n", __func__);
		}
		return B_OK;
	}
// SPDX-SnippetEnd

	RingQueue* ring = gInfo->ringQueue[ringType];
	if (ring == NULL) {
		ERROR("%s: Specified ring doesn't exist!\n", __func__);
		return B_ERROR;
	}

	// We don't execute this code until it's more complete.
	ERROR("%s: TODO\n", __func__);
	return B_OK;


	// TODO: Write initial ring PACKET3 STATE

	// *** r600_cp_init_ring_buffer
	// Reset command processor
	Write32(OUT, GRBM_SOFT_RESET, RADEON_SOFT_RESET_CP);
	Read32(OUT, GRBM_SOFT_RESET);
	snooze(15000);
	Write32(OUT, GRBM_SOFT_RESET, 0);

	// Set ring buffer size
	uint32 controlScratch = RB_NO_UPDATE
		| (compute_order(4096 / 8) << 8) // rptr_update_l2qw
		| compute_order(ring->GetSize() / 8); // size_l2qw
	#ifdef __BIG_ENDIAN
	controlScratch |= BUF_SWAP_32BIT;
	#endif
	Write32(OUT, CP_RB_CNTL, controlScratch);

	// Set delays and timeouts
	Write32(OUT, CP_SEM_WAIT_TIMER, 0);
	Write32(OUT, CP_RB_WPTR_DELAY, 0);

	// Enable RenderBuffer Reads
	controlScratch |= RB_RPTR_WR_ENA;
	Write32(OUT, CP_RB_CNTL, controlScratch);

	// Zero out command processor read and write pointers
	Write32(OUT, CP_RB_RPTR_WR, 0);
	Write32(OUT, CP_RB_WPTR, 0);

	#if 0
	int ringPointer = 0;
	// TODO: AGP cards
	/*
	if (RADEON_IS_AGP) {
		ringPointer = dev_priv->ring_rptr->offset
			- dev->agp->base
			+ dev_priv->gart_vm_start;
	} else {
	*/
	ringPointer = dev_priv->ring_rptr->offset
		- ((unsigned long) dev->sg->virtual)
		+ dev_priv->gart_vm_start;

	Write32(OUT, CP_RB_RPTR_ADDR, (ringPointer & 0xfffffffc));
	Write32(OUT, CP_RB_RPTR_ADDR_HI, upper_32_bits(ringPointer));

	// Drop RPTR_WR_ENA and update CP RB Control
	controlScratch &= ~R600_RB_RPTR_WR_ENA;
	Write32(OUT, CP_RB_CNTL, controlScratch);
	#endif

	#if 0
	// Update command processor pointer
	int commandPointer = 0;

	// TODO: AGP cards
	/*
	if (RADEON_IS_AGP) {
		commandPointer = (dev_priv->cp_ring->offset
			- dev->agp->base
			+ dev_priv->gart_vm_start);
	}
	*/
	commandPointer = (dev_priv->cp_ring->offset
		- (unsigned long)dev->sg->virtual
		+ dev_priv->gart_vm_start);
	#endif

	#if 0
	Write32(OUT, CP_RB_BASE, commandPointer >> 8);
	Write32(OUT, CP_ME_CNTL, 0xff);
	Write32(OUT, CP_DEBUG, (1 << 27) | (1 << 28));
	#endif

	#if 0
	// Initialize scratch register pointer.
	// This wil lcause the scratch register values to be wtitten
	// to memory whenever they are updated.

	uint64 scratchAddr = Read32(OUT, CP_RB_RPTR_ADDR) & 0xFFFFFFFC;
	scratchAddr |= ((uint64)Read32(OUT, CP_RB_RPTR_ADDR_HI)) << 32;
	scratchAddr += R600_SCRATCH_REG_OFFSET;
	scratchAddr >>= 8;
	scratchAddr &= 0xffffffff;

	Write32(OUT, R600_SCRATCH_ADDR, (uint32)scratchAddr);

	Write32(OUT, R600_SCRATCH_UMSK, 0x7);
	#endif

	#if 0
	// Enable bus mastering
	radeon_enable_bm(dev_priv);

	radeon_write_ring_rptr(dev_priv, R600_SCRATCHOFF(0), 0);
	Write32(OUT, R600_LAST_FRAME_REG, 0);

	radeon_write_ring_rptr(dev_priv, R600_SCRATCHOFF(1), 0);
	Write32(OUT, R600_LAST_DISPATCH_REG, 0);

	radeon_write_ring_rptr(dev_priv, R600_SCRATCHOFF(2), 0);
	Write32(OUT, R600_LAST_CLEAR_REG, 0);
	#endif

	// Reset sarea?
	#if 0
	master_priv = file_priv->master->driver_priv;
	if (master_priv->sarea_priv) {
		master_priv->sarea_priv->last_frame = 0;
		master_priv->sarea_priv->last_dispatch = 0;
		master_priv->sarea_priv->last_clear = 0;
	}

	r600_do_wait_for_idle(dev_priv);
	#endif

	return B_OK;
}


status_t
radeon_gpu_ss_control(pll_info* pll, bool enable)
{
	TRACE("%s called\n", __func__);

	radeon_shared_info &info = *gInfo->shared_info;
	uint32 ssControl;

	if (info.chipsetID >= RADEON_CEDAR) {
		switch (pll->id) {
			case ATOM_PPLL1:
				// PLL1
				ssControl = Read32(OUT, EVERGREEN_P1PLL_SS_CNTL);
				if (enable)
					ssControl |= EVERGREEN_PxPLL_SS_EN;
				else
					ssControl &= ~EVERGREEN_PxPLL_SS_EN;
				Write32(OUT, EVERGREEN_P1PLL_SS_CNTL, ssControl);
				break;
			case ATOM_PPLL2:
				// PLL2
				ssControl = Read32(OUT, EVERGREEN_P2PLL_SS_CNTL);
				if (enable)
					ssControl |= EVERGREEN_PxPLL_SS_EN;
				else
					ssControl &= ~EVERGREEN_PxPLL_SS_EN;
				Write32(OUT, EVERGREEN_P2PLL_SS_CNTL, ssControl);
				break;
		}
		// DCPLL, no action
		return B_OK;
	} else if (info.chipsetID >= RADEON_RS600) {
		switch (pll->id) {
			case ATOM_PPLL1:
				// PLL1
				ssControl = Read32(OUT, AVIVO_P1PLL_INT_SS_CNTL);
				if (enable)
					ssControl |= 1;
				else
					ssControl &= ~1;
				Write32(OUT, AVIVO_P1PLL_INT_SS_CNTL, ssControl);
				break;
			case ATOM_PPLL2:
				// PLL2
				ssControl = Read32(OUT, AVIVO_P2PLL_INT_SS_CNTL);
				if (enable)
					ssControl |= 1;
				else
					ssControl &= ~1;
				Write32(OUT, AVIVO_P2PLL_INT_SS_CNTL, ssControl);
				break;
		}
		// DCPLL, no action
		return B_OK;
	}

	return B_ERROR;
}


void
radeon_gpu_dump_registers()
{
	radeon_shared_info &info = *gInfo->shared_info;

	if (info.dceMajor < 4) {
		// R600/R700 (DCE 2.0/3.0): fixed D1/D2 register blocks
		TRACE("D1CRTC_STATUS        Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D1CRTC_STATUS));
		TRACE("D2CRTC_STATUS        Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D2CRTC_STATUS));
		TRACE("D1CRTC_CONTROL       Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D1CRTC_CONTROL));
		TRACE("D2CRTC_CONTROL       Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D2CRTC_CONTROL));
		TRACE("D1GRPH_ENABLE        Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D1GRPH_ENABLE));
		TRACE("D2GRPH_ENABLE        Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D2GRPH_ENABLE));
		TRACE("D1SCL_ENABLE         Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D1SCL_SCALER_ENABLE));
		TRACE("D2SCL_ENABLE         Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D2SCL_SCALER_ENABLE));
		TRACE("D1CRTC_BLANK_CONTROL Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D1CRTC_BLANK_CONTROL));
		TRACE("D2CRTC_BLANK_CONTROL Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, AVIVO_D2CRTC_BLANK_CONTROL));
	} else {
		// Evergreen+ (DCE 4.0+): strided per-CRTC register blocks.
		TRACE("CRTC0_STATUS        Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, EVERGREEN_CRTC_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET));
		TRACE("CRTC1_STATUS        Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, EVERGREEN_CRTC_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET));
		TRACE("CRTC0_CONTROL       Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC0_REGISTER_OFFSET));
		TRACE("CRTC1_CONTROL       Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, EVERGREEN_CRTC_CONTROL + EVERGREEN_CRTC1_REGISTER_OFFSET));
		TRACE("GRPH0_ENABLE        Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, EVERGREEN_GRPH_ENABLE + EVERGREEN_CRTC0_REGISTER_OFFSET));
		TRACE("GRPH1_ENABLE        Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, EVERGREEN_GRPH_ENABLE + EVERGREEN_CRTC1_REGISTER_OFFSET));
		TRACE("CRTC0_BLANK_CONTROL Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, EVERGREEN_CRTC_BLANK_CONTROL + EVERGREEN_CRTC0_REGISTER_OFFSET));
		TRACE("CRTC1_BLANK_CONTROL Value: 0x%" B_PRIX32 "\n",
			Read32(CRT, EVERGREEN_CRTC_BLANK_CONTROL + EVERGREEN_CRTC1_REGISTER_OFFSET));
	}
}
