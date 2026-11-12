/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 * Copyright 2020 Advanced Micro Devices, Inc.
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * The SMU 12 mode-2 reset, from the kernel, for the M10 hang policy.
 * Ported from Linux v6.12, all MIT files of AMD (headers checked):
 *  - drivers/gpu/drm/amd/pm/swsmu/smu12/smu_v12_0.c:208-211
 *    smu_v12_0_mode2_reset: one message, SMU_MSG_GfxDeviceDriverReset,
 *    parameter SMU_RESET_MODE_2 (Copyright 2019 AMD);
 *  - drivers/gpu/drm/amd/pm/swsmu/smu_cmn.c:109-123 __smu_cmn_poll_stat,
 *    227-236 __smu_cmn_send_msg, 407-468 smu_cmn_send_smc_msg_with_param
 *    (Copyright 2020 AMD): wait for a non-zero response, then write the
 *    response 0, the parameter, the message, and wait again. The wait is
 *    usec_timeout * 20 one-microsecond steps, 2 s at the default 100 ms.
 *    SMU_RESP_OK is 1 (smu_cmn.c:82);
 *  - drivers/gpu/drm/amd/pm/swsmu/smu12/renoir_ppt.c:44-51 and 1516-1518:
 *    message C2PMSG_66 (0x0282), parameter C2PMSG_82 (0x0292), response
 *    C2PMSG_90 (0x029a), MP1 segment 0;
 *  - drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v12_0_ppsmc.h:63
 *    PPSMC_MSG_GfxDeviceDriverReset 0x1E (Copyright 2019 AMD);
 *  - drivers/gpu/drm/amd/pm/swsmu/inc/amdgpu_smu.h:443 SMU_RESET_MODE_2,
 *    the value 2 of enum smu_reset_mode;
 *  - drivers/gpu/drm/amd/include/renoir_ip_offset.h:156 MP1_BASE
 *    instance 0 segment 0 is 0x00016000 (Copyright (C) 2019 AMD).
 * No GPL code. This is not the accelerant's copy in gfx9_start.cpp,
 * which stays part of the fixed ring start.
 */

#include "radeon_hd_private.h"

#include <KernelExport.h>
#include <OS.h>


static const uint32 kMp1Seg0 = 0x00016000;
static const uint32 kSmnC2pMsg66 = 0x0282;
static const uint32 kSmnC2pMsg82 = 0x0292;
static const uint32 kSmnC2pMsg90 = 0x029a;
static const uint32 kMsgGfxDeviceDriverReset = 0x1E;
static const uint32 kResetMode2 = 2;
static const uint32 kRespNone = 0;
static const uint32 kRespOk = 1;
/* adev->usec_timeout (AMDGPU_MAX_USEC_TIMEOUT default 100000) * 20. */
static const int32 kPollSteps = 100000 * 20;


static uint32
smu_byte(uint32 index)
{
	return (kMp1Seg0 + index) * 4u;
}


static bool
smu_fits(radeon_info* info, uint32 offset)
{
	area_info area;
	if (info == NULL || info->registers == 0 || info->registers_area < 0)
		return false;
	if (get_area_info(info->registers_area, &area) != B_OK)
		return false;
	return (uint64)offset + sizeof(uint32) <= area.size;
}


static uint32
smu_read(radeon_info* info, uint32 offset)
{
	return *(volatile uint32*)(info->registers + offset);
}


static void
smu_write(radeon_info* info, uint32 offset, uint32 value)
{
	*(volatile uint32*)(info->registers + offset) = value;
}


/* __smu_cmn_poll_stat: the response register, or 0 after the timeout. */
static uint32
smu_poll(radeon_info* info)
{
	uint32 reg = kRespNone;
	for (int32 timeout = kPollSteps; timeout > 0; timeout--) {
		reg = smu_read(info, smu_byte(kSmnC2pMsg90));
		if (reg != kRespNone)
			break;
		spin(1);
	}
	return reg;
}


/* smu_v12_0_mode2_reset through smu_cmn_send_smc_msg_with_param. The
 * SMU has answered messages since boot (the ring start sent one), so
 * the first poll is not skipped. A busy SMU (still 0 after the wait)
 * gets no message. Returns true when the SMU answered SMU_RESP_OK. */
extern "C" bool
r5_smu12_mode2_reset(radeon_info* info, uint32* before, uint32* after)
{
	uint32 resp = smu_byte(kSmnC2pMsg90);
	if (before != NULL)
		*before = 0xffffffffu;
	if (after != NULL)
		*after = 0xffffffffu;
	if (!smu_fits(info, smu_byte(kSmnC2pMsg66))
		|| !smu_fits(info, smu_byte(kSmnC2pMsg82)) || !smu_fits(info, resp))
		return false;
	uint32 reg = smu_poll(info);
	if (before != NULL)
		*before = reg;
	if (reg == kRespNone)
		return false;
	smu_write(info, resp, 0);
	smu_write(info, smu_byte(kSmnC2pMsg82), kResetMode2);
	smu_write(info, smu_byte(kSmnC2pMsg66), kMsgGfxDeviceDriverReset);
	reg = smu_poll(info);
	if (after != NULL)
		*after = reg;
	return reg == kRespOk;
}
