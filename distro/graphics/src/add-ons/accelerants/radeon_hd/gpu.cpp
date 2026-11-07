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
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <driver_settings.h>
#include <stdarg.h>
#include "gfx9_start.h"
#include "r5_amd_device.h"
// SPDX-SnippetEnd

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
// The doorbell is an area clone. It stays in the accelerant. The kernel
// maps the same BAR before the accelerant exists.
// AMDGPU_DOORBELL64_GFX_RING0 is qword 0x8b, shifted to byte 0x458.
static const uint32 kGfx9DoorbellByte = 0x458;

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
r5_host_read32(void* cookie, uint32 offset)
{
	(void)cookie;
	return Read32(OUT, offset);
}


static void
r5_host_write32(void* cookie, uint32 offset, uint32 value)
{
	(void)cookie;
	Write32(OUT, offset, value);
}


static bool
r5_host_fits(void* cookie, uint32 offset)
{
	(void)cookie;
	area_info info;
	if (gInfo == NULL || gInfo->regs_area < 0)
		return false;
	if (get_area_info(gInfo->regs_area, &info) != B_OK)
		return false;
	return (uint64)offset + sizeof(uint32) <= info.size;
}


static void
r5_host_sleep(void* cookie, uint32 micros)
{
	(void)cookie;
	snooze(micros);
}


static void
r5_host_log(void* cookie, const char* format, ...)
{
	(void)cookie;
	char line[640];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	_sPrintf("%s", line);
}


static status_t
r5_host_vram(void* cookie, uint64 bytes, uint64 align, uint64* offset,
	uint8** address)
{
	(void)cookie;
	return radeon_vram_alloc(bytes, align, offset, address);
}


static uint8*
r5_host_firmware(void* cookie, const char* name, size_t* bytes)
{
	(void)cookie;
	return radeon_read_named_firmware(name, bytes);
}


static void
r5_host_free_firmware(void* cookie, uint8* blob)
{
	(void)cookie;
	free(blob);
}


static void
r5_host_fill(r5_gfx9_host* host)
{
	host->read32 = r5_host_read32;
	host->write32 = r5_host_write32;
	host->mmio_fits = r5_host_fits;
	host->sleep_us = r5_host_sleep;
	host->log = r5_host_log;
	host->vram_alloc = r5_host_vram;
	host->read_firmware = r5_host_firmware;
	host->free_firmware = r5_host_free_firmware;
	host->doorbell = sGfx9Doorbell;
	host->doorbellBytes = sGfx9DoorbellBytes;
	host->cookie = NULL;
}


static void
radeon_gfx9_write_text()
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
	fputs(gInfo->shared_info->gfx9_text, file);
	fclose(file);
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
		if (gInfo->shared_info->gfx9_owner == R5_GFX9_OWNER_KERNEL) {
			ERROR("%s: R5 radeon_hd: GFX9 owned by the kernel\n",
				__func__);
			radeon_gfx9_write_text();
			return B_OK;
		}
		bool gfx9Ring = false;
		bool workload = false;
		bool compute = false;
		void* settings = load_driver_settings("radeon_hd");
		if (settings != NULL) {
			gfx9Ring = get_driver_boolean_parameter(settings, "gfx9_ring",
				false, true);
			workload = get_driver_boolean_parameter(settings,
				"gfx9_workload", false, true);
			compute = get_driver_boolean_parameter(settings,
				"gfx9_compute", false, true);
			unload_driver_settings(settings);
		}
		if (!gfx9Ring) {
			ERROR("%s: R5 radeon_hd: GFX9 ring off, set gfx9_ring in the"
				" radeon_hd driver settings\n", __func__);
			return B_OK;
		}
		if (!workload)
			ERROR("%s: R5 radeon_hd: GFX9 workload off\n", __func__);
		if (!compute)
			ERROR("%s: R5 radeon_hd: GFX9 compute off\n", __func__);
		uint16 chipsetID = gInfo->shared_info->chipsetID;
		if (ringType == RADEON_QUEUE_TYPE_GFX_INDEX
			&& chipsetID >= RADEON_RAVEN && chipsetID <= RADEON_RENOIR) {
			r5_gfx9_host host;
			r5_host_fill(&host);
			r5_gfx9_prepare(&host, gInfo->fb.vramSize);
			uint32 pciID = gInfo->shared_info->pciID;
			if (r5_amd_gfx9_cp(pciID)) {
				r5_gfx9_results results;
				memset(&results, 0, sizeof(results));
				r5_gfx9_start(&host, pciID, gInfo->fb.vramSize,
					workload, compute, &results);
				snprintf(gInfo->shared_info->gfx9_text,
					sizeof(gInfo->shared_info->gfx9_text), "%s",
					results.text);
				gInfo->shared_info->gfx9_text_used = results.used;
				radeon_gfx9_write_text();
			} else if (r5_amd_lucienne(pciID)) {
				ERROR("%s: R5 radeon_hd: Lucienne firmware stays unloaded, "
					"mode 2 not sent\n", __func__);
			} else {
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
