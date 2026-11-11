/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AMD backend for the render node /dev/gpu/radeon_hd/<n>. The graphics
 * node is unchanged. This file fills r5_gpu_backend_ops and keeps the
 * calls into r5_gfx9_*. The core does not name AMD register types.
 */

#include "radeon_hd_private.h"
#include "gfx9_start.h"
#include "r5_gpu_internal.h"
#include "r5_cs.h"
#include "r5_log.h"
#include "r5_wait.h"
#include "r5_stage_p.h"
#include "r5_stage_r1.h"
#include "r5_passport.h"

#include <KernelExport.h>
#include <kernel.h>

#include <driver_settings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


area_id r5_gfx9_map_vram(uint64 offset, uint64 bytes, void** address);


extern radeon_info* gDeviceInfo[];


static const uint32 kGcSegment = 0x2000;
static const uint32 kNbioSegment = 0x0D20;
static const uint32 kStrapIndex = 0x000f;
static const uint32 kStrapMask = 0x0F000000u;
static const uint32 kStrapShift = 24;
static const uint32 kShaderArrayConfig = 0x026f;
static const uint32 kUserShaderArrayConfig = 0x0270;
static const uint32 kGbAddrConfig = 0x063e;
static const uint32 kRbBackendDisable = 0x063d;
static const uint32 kUserRbBackendDisable = 0x06df;
static const uint32 kRbDisableShift = 16;
static const uint32 kRbDisableMask = 0x00ff0000u;
/* gfx_v9_0 for IP 9.3.0: one shader engine, one array, two backends. */
static const uint32 kMaxBackendsPerSe = 2;
static const uint32 kInactiveShift = 16;
static const uint32 kInactiveMask = 0xffff0000u;

/* Kernel jobs (mark, stage P, the copies) use fence context 0. */
static int32 sKernelPreamble;


static void gpu_ib_words(const char* why);
static void gpu_dump_priv();


void
gpu_cache_log_settings()
{
	bool debugOn = false;
	bool selftestOn = false;
	bool submitLogOn = false;
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		debugOn = get_driver_boolean_parameter(settings, "gpu_debug",
			false, true);
		selftestOn = get_driver_boolean_parameter(settings, "gpu_selftest",
			false, true);
		submitLogOn = get_driver_boolean_parameter(settings, "gpu_submit_log",
			false, true);
		r5_gfx9_set_flush_fence(get_driver_boolean_parameter(settings,
			"gfx9_flush_fence", false, true) ? 1 : 0);
		r5_gfx9_set_pasid_map(get_driver_boolean_parameter(settings,
			"gfx9_pasid_map", false, true) ? 1 : 0);
		r5_gfx9_set_ce_meta(get_driver_boolean_parameter(settings,
			"gfx9_ce_meta", false, true) ? 1 : 0);
		r5_gfx9_set_cond_exec(get_driver_boolean_parameter(settings,
			"gfx9_cond_exec", false, true) ? 1 : 0);
		r5_gfx9_set_gds_switch(get_driver_boolean_parameter(settings,
			"gfx9_gds_switch", false, true) ? 1 : 0);
		sDeferred = get_driver_boolean_parameter(settings,
			"gfx9_deferred_free", false, true) ? 1 : 0;
		unload_driver_settings(settings);
	} else {
		r5_gfx9_set_flush_fence(0);
		r5_gfx9_set_pasid_map(0);
		r5_gfx9_set_ce_meta(0);
		r5_gfx9_set_cond_exec(0);
		r5_gfx9_set_gds_switch(0);
		sDeferred = 0;
	}
	r5_gfx9_set_defer_stamp(sDeferred ? gpu_defer_stamp : 0);
	sDebugOn = debugOn ? 1 : 0;
	sSelftestOn = selftestOn ? 1 : 0;
	sSubmitLogOn = submitLogOn ? 1 : 0;
}


static uint32
gpu_mmio(radeon_info* info, uint32 segment, uint32 index)
{
	uint32 byte = (segment + index) * 4u;
	if (info == NULL || info->registers_area < 0 || info->registers == 0)
		return 0xffffffffu;
	return *(volatile uint32*)(info->registers + byte);
}


static uint32
gpu_strap_rev(radeon_info* info)
{
	/* nbio_v7_0_get_rev_id: mmRCC_DEV0_EPF0_STRAP0 in NBIO segment 2. */
	uint32 strap = gpu_mmio(info, kNbioSegment, kStrapIndex);
	return (strap & kStrapMask) >> kStrapShift;
}


static uint32
gpu_active_cus(radeon_info* info, uint32 cuPerSh, uint32* _bitmap)
{
	if (cuPerSh == 0 || cuPerSh > 16)
		return 0;
	uint32 cc = gpu_mmio(info, kGcSegment, kShaderArrayConfig);
	uint32 user = gpu_mmio(info, kGcSegment, kUserShaderArrayConfig);
	uint32 inactive = ((cc | user) & kInactiveMask) >> kInactiveShift;
	uint32 mask = (1u << cuPerSh) - 1u;
	uint32 bitmap = (~inactive) & mask;
	uint32 count = 0;
	for (uint32 bit = bitmap; bit != 0; bit >>= 1)
		count += bit & 1u;
	if (_bitmap != NULL)
		*_bitmap = bitmap;
	if (gpu_debug_enabled())
	dprintf("radeon_hd: t %lld gfx9 cu cc %08lx user %08lx active %lu bitmap %08lx\n",
		(long long)system_time(),
		(unsigned long)cc, (unsigned long)user, (unsigned long)count,
		(unsigned long)bitmap);
	return count;
}


/* gfx_v9_0_get_rb_active_bitmap and gfx_v9_0_setup_rb. One SE and one
 * SH, so this does not write GRBM_GFX_INDEX. num is the pipe count
 * Linux puts in num_rb_pipes (max backends times shader engines).
 * mask is backend_enable_mask. */
static void
gpu_rb(radeon_info* info, uint32 rbPerSe, uint32* num, uint32* mask)
{
	if (rbPerSe == 0 || rbPerSe > 8)
		rbPerSe = kMaxBackendsPerSe;
	uint32 cc = gpu_mmio(info, kGcSegment, kRbBackendDisable);
	uint32 user = gpu_mmio(info, kGcSegment, kUserRbBackendDisable);
	uint32 disabled = ((cc | user) & kRbDisableMask) >> kRbDisableShift;
	uint32 bits = (1u << rbPerSe) - 1u;
	uint32 active = (~disabled) & bits;
	if (num != NULL)
		*num = kMaxBackendsPerSe;
	if (mask != NULL)
		*mask = active;
	if (r5_log_allow(0, 0, R5_LOG_ONCE, &sErrorLines, &sOnceBits, R5_ONCE_RB))
	dprintf("radeon_hd: rb num %lu mask %lx cc %08lx user %08lx\n",
		(unsigned long)kMaxBackendsPerSe, (unsigned long)active,
		(unsigned long)cc, (unsigned long)user);
}








static status_t
gpu_get_info(gpu_client* client, void* buffer, size_t length)
{
	radeon_info* info = client != NULL ? (radeon_info*)client->info : NULL;
	if (info == NULL || info->pci == NULL || buffer == NULL || length < 8)
		return B_BAD_VALUE;
	if (!is_user_address_range(buffer, length))
		return B_BAD_ADDRESS;
	uint32 claimed = 0;
	if (user_memcpy(&claimed, buffer, sizeof(claimed)) != B_OK)
		return B_BAD_ADDRESS;
	if (claimed > length)
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
	gpu_lock();
	if (sLost)
		out.capabilities |= R5_GPU_CAP_LOST;
	if (sLost && client != NULL
		&& (client == sGuilty || client->faulted))
		out.capabilities |= R5_GPU_CAP_GUILTY;
	out.va_offset = R5_GPU_VA_OFFSET;
	out.va_max = R5_GPU_VA_MAX;
	/* amdgpu_kms.c for this VM: the hole ends at AMDGPU_GMC_HOLE_END
	 * and the high manager runs up to the byte size minus the reserved
	 * top. The page tables are the existing 4-level tables. */
	out.high_va_offset = R5_GPU_HIGH_VA_OFFSET;
	out.high_va_max = R5_GPU_HIGH_VA_MAX;
	out.vendor_block_size = (uint32)sizeof(r5_gpu_amd_info);
	out.vendor_block_version = 1;
	out.bus = info->pci->bus;
	out.dev = info->pci->device;
	out.func = info->pci->function;
	out.subsystem_vendor = info->pci->u.h0.subsystem_vendor_id;
	out.subsystem_device = info->pci->u.h0.subsystem_id;
	if (info->shared_info != NULL) {
		out.vram_bytes = (uint64)info->shared_info->frame_buffer_size * 1024ull;
		out.gtt_bytes = out.vram_bytes;
	}
	for (uint32 i = 0; i < kMaxBo; i++) {
		if (!sBo[i].live)
			continue;
		if (sBo[i].domain == R5_GPU_DOMAIN_VRAM)
			out.vram_usage += sBo[i].bytes;
		else if (sBo[i].domain == R5_GPU_DOMAIN_GTT)
			out.gtt_usage += sBo[i].bytes;
	}
	/* The whole BAR is CPU-visible on this APU. There is no invisible heap. */
	out.visible_vram_usage = out.vram_usage;
	gpu_unlock();
	size_t copy = sizeof(out);
	if (length < copy)
		copy = length;
	if (user_memcpy(buffer, &out, copy) != B_OK)
		return B_BAD_ADDRESS;
	if (length < sizeof(out) + sizeof(r5_gpu_amd_info))
		return B_OK;

	r5_gpu_amd_info amd;
	memset(&amd, 0, sizeof(amd));
	amd.size = (uint32)sizeof(amd);
	amd.version = 1;
	uint32 strap = gpu_strap_rev(info);
	uint32 bitmap = 0;
	amd.device.device_id = info->pci->device_id;
	amd.device.chip_rev = strap;
	amd.device.external_rev = r5_amd_external_rev(info->pci->device_id, strap);
	amd.device.pci_rev = info->pci->revision;
	uint32 cuPerSh = 0;
	uint32 rbPerSe = 0;
	r5_passport_apply(info, &amd, &cuPerSh, &rbPerSe);
	amd.device.cu_active_number = gpu_active_cus(info, cuPerSh, &bitmap);
	amd.device.cu_bitmap[0][0] = bitmap;
	{
		uint32 rbNum = 0;
		uint32 rbMask = 0;
		gpu_rb(info, rbPerSe, &rbNum, &rbMask);
		amd.device.num_rb_pipes = rbNum;
		amd.device.enabled_rb_pipes_mask = rbMask;
	}
	/* Default VM fragment size 9. (1 << 9) * 4096. */
	amd.device.pte_fragment_size = 2097152;
	amd.device.virtual_address_offset = R5_GPU_VA_OFFSET;
	amd.device.virtual_address_max = R5_GPU_VA_MAX;
	amd.device.virtual_address_alignment = 4096;
	amd.device.gart_page_size = 4096;
	amd.device.high_va_offset = R5_GPU_HIGH_VA_OFFSET;
	amd.device.high_va_max = R5_GPU_HIGH_VA_MAX;
	if (amd.hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_major == 0) {
		amd.hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_major = 9;
		amd.hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_minor = 3;
	}
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].ib_start_alignment = 32;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].ib_size_alignment = 4;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].available_rings = 1;
	amd.hw_ip[R5_AMDGPU_HW_IP_COMPUTE] = amd.hw_ip[R5_AMDGPU_HW_IP_GFX];
	amd.hw_ip[R5_AMDGPU_HW_IP_COMPUTE].available_rings = 0;
	for (uint32 i = 0; i < R5_AMDGPU_FW_COUNT && i < 32; i++) {
		amd.firmware[i].ver = info->fw_version[i];
		amd.firmware[i].feature = info->fw_feature[i];
	}
	if (user_memcpy((uint8*)buffer + sizeof(out), &amd, sizeof(amd)) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}






static uint64
gpu_pte_flags(uint32 flags)
{
	uint64 bits = (uint64)R5_GFX9_PTE_VALID | (uint64)R5_GFX9_PTE_READABLE;
	if ((flags & R5_GPU_VA_WRITE) != 0)
		bits |= (uint64)R5_GFX9_PTE_WRITEABLE;
	if ((flags & R5_GPU_VA_EXEC) != 0)
		bits |= (uint64)R5_GFX9_PTE_EXECUTABLE;
	return bits;
}


static void
gpu_mark(uint64 va)
{
	uint32 word = 0;
	uint32 got = 0;
	uint64 pte = 0;
	if (va != 0) {
		got = r5_gfx9_vmid1_copy(va, &word, 1);
		r5_gfx9_vmid1_pte(va, &pte);
	}
	/* The page number sits above the flag bits. */
	uint64 pa = pte & ~0xfffull;
	if (got == 1) {
		dprintf("radeon_hd: mark 0x%llx = %08lx pte %016llx pa %016llx\n",
			(unsigned long long)va, (unsigned long)word,
			(unsigned long long)pte, (unsigned long long)pa);
	} else {
		dprintf("radeon_hd: mark 0x%llx unmapped pte %016llx pa %016llx\n",
			(unsigned long long)va, (unsigned long long)pte,
			(unsigned long long)pa);
	}
}


static void gpu_dump_one(const char* why, const char* which, uint64 base,
	uint32 hint, uint32 cpOffset, int depth);


static void
gpu_pm4_walk(const char* why, const uint32* words, uint32 count, int depth)
{
	uint32 i = 0;
	while (i < count) {
		uint32 header = words[i];
		uint32 type = header >> 30;
		if (type == 2) {
			i++;
			continue;
		}
		if (type != 3 || i + 1 >= count) {
			i++;
			continue;
		}
		uint32 opcode = (header >> 8) & 0xffu;
		uint32 body = ((header >> 16) & 0x3fffu) + 1u;
		if (i + 1u + body > count)
			break;
		const char* name = NULL;
		uint32 reg = 0;
		uint32 values = 0;
		if (opcode == 0x68 || opcode == 0x69 || opcode == 0x76
			|| opcode == 0x79 || opcode == 0x7a) {
			uint32 base = 0x8000;
			if (opcode == 0x69)
				base = 0x28000;
			else if (opcode == 0x76)
				base = 0xb000;
			else if (opcode == 0x79 || opcode == 0x7a)
				base = 0x30000;
			reg = base + ((words[i + 1] & 0xffffu) << 2);
			values = body > 0 ? body - 1u : 0;
			if (opcode == 0x68)
				name = "SET_CONFIG_REG";
			else if (opcode == 0x69)
				name = "SET_CONTEXT_REG";
			else if (opcode == 0x76)
				name = "SET_SH_REG";
			else if (opcode == 0x79)
				name = "SET_UCONFIG_REG";
			else
				name = "SET_UCONFIG_REG_INDEX";
		} else if (opcode == 0x37 && body >= 1) {
			uint32 dst = (words[i + 1] >> 8) & 0xfu;
			if (dst == 5 && body >= 3) {
				gpu_mark((uint64)words[i + 2]
					| ((uint64)words[i + 3] << 32));
			} else if (dst == 0 && body >= 3) {
				name = "WRITE_DATA";
				reg = words[i + 2];
				values = 1;
			}
		} else if (opcode == 0x49 && body >= 4) {
			gpu_mark((uint64)words[i + 3] | ((uint64)words[i + 4] << 32));
		} else if (opcode == 0x40 && body >= 4) {
			uint32 dst = (words[i + 1] >> 8) & 0xfu;
			if (dst == 0 || dst == 4) {
				name = "COPY_DATA";
				reg = words[i + 4] << 2;
				values = 1;
			}
		} else if (opcode == 0x3c && body >= 2) {
			if ((words[i + 1] & (3u << 4)) == 0) {
				name = "WAIT_REG_MEM";
				reg = words[i + 2];
				values = 1;
			}
		} else if (opcode == 0x3f && body >= 3 && depth == 0) {
			uint64 child = (uint64)words[i + 1]
				| ((uint64)words[i + 2] << 32);
			uint32 childDw = words[i + 3] & 0xfffffu;
			if (gpu_dump_line())
				dprintf("radeon_hd: pm4 +%04lx INDIRECT_BUFFER va %016llx dw %lu\n",
					(unsigned long)i, (unsigned long long)child,
					(unsigned long)childDw);
			gpu_dump_one(why, "nested", child, childDw, 0, 1);
		}
		if (name != NULL && gpu_dump_line()) {
			if (opcode == 0x68 || opcode == 0x69 || opcode == 0x76
				|| opcode == 0x79 || opcode == 0x7a) {
				dprintf("radeon_hd: pm4 +%04lx %s reg 0x%lx idx %lu count %lu\n",
					(unsigned long)i, name, (unsigned long)reg,
					(unsigned long)(words[i + 1] >> 28),
					(unsigned long)values);
			} else {
				dprintf("radeon_hd: pm4 +%04lx %s reg 0x%lx count %lu\n",
					(unsigned long)i, name, (unsigned long)reg,
					(unsigned long)values);
			}
		}
		i += 1u + body;
	}
}


static void
gpu_dump_one(const char* why, const char* which, uint64 base, uint32 hint,
	uint32 cpOffset, int depth)
{
	uint32 words[1024];
	uint32 want = hint;
	memset(words, 0, sizeof(words));
	if (want == 0 || want > 1024)
		want = 1024;
	uint32 got = 0;
	if (base != 0)
		got = r5_gfx9_vmid1_copy(base, words, want);
	if (depth == 0 && !gpu_dump_begin())
		return;
	if (!gpu_dump_line())
		return;
	dprintf("radeon_hd: %s %s va %016llx dw %lu got %lu cp_off %lu\n",
		why == NULL ? "ib" : why, which == NULL ? "ib" : which,
		(unsigned long long)base, (unsigned long)want,
		(unsigned long)got, (unsigned long)cpOffset);
	for (uint32 i = 0; i < got; i += 8) {
		uint32 line[8];
		uint32 n = got - i;
		memset(line, 0, sizeof(line));
		if (n > 8)
			n = 8;
		memcpy(line, words + i, n * 4);
		if (!gpu_dump_line())
			break;
		dprintf("radeon_hd: ib +%04lx %08lx %08lx %08lx %08lx "
			"%08lx %08lx %08lx %08lx\n",
			(unsigned long)i,
			(unsigned long)line[0], (unsigned long)line[1],
			(unsigned long)line[2], (unsigned long)line[3],
			(unsigned long)line[4], (unsigned long)line[5],
			(unsigned long)line[6], (unsigned long)line[7]);
	}
	gpu_pm4_walk(why, words, got, depth);
}


static void
gpu_dump_regs(const char* why, const r5_gfx9_ib_now* ib, const char* which)
{
	if (ib == NULL || ib->base == 0)
		return;
	uint32 known = gpu_known_dw(ib->base);
	uint32 hint = known != 0 ? known : 1024;
	gpu_dump_one(why, which, ib->base, hint, ib->offset, 0);
}


static void
gpu_ib_words(const char* why)
{
	r5_gfx9_ib_now ib1;
	r5_gfx9_ib_now ib2;
	memset(&ib1, 0, sizeof(ib1));
	memset(&ib2, 0, sizeof(ib2));
	r5_gfx9_ib_regs(&ib1, &ib2);
	gpu_dump_regs(why, &ib1, "ib1");
	if (ib2.base != ib1.base)
		gpu_dump_regs(why, &ib2, "ib2");
	if (ib1.base == 0 && ib2.base == 0 && !gpu_dump_begin())
		return;
	if (gpu_dump_line())
	dprintf("radeon_hd: %s roq ib1 %08lx\n",
		why == NULL ? "ib" : why, (unsigned long)r5_gfx9_roq_ib1());
	if (sInfo != NULL) {
		uint64 pte = 0;
		uint64 va = ib1.base != 0 ? ib1.base : sLastIbVa;
		if (va != 0)
			r5_gfx9_vmid1_pte(va, &pte);
		r5_gfx9_cp_dump(why == NULL ? "ib" : why, pte);
	}
}


static void
gpu_dump_priv()
{
	r5_gfx9_ib_now ib1;
	r5_gfx9_ib_now ib2;
	if (!r5_gfx9_take_priv(&ib1, &ib2))
		return;
	gpu_dump_regs("ih", &ib1, "ib1");
	if (ib2.base != ib1.base)
		gpu_dump_regs("ih", &ib2, "ib2");
	dprintf("radeon_hd: ih roq ib1 %08lx\n",
		(unsigned long)r5_gfx9_roq_ib1());
	r5_gfx9_pipe_log("ih");
}


static void
gpu_stage_words(const char* name, uint64 ibVa, uint8* cpu, uint64 phys)
{
	uint32 words[8];
	memset(words, 0, sizeof(words));
	if (cpu != NULL)
		memcpy(words, cpu, sizeof(words));
	dprintf("radeon_hd: %s ib %016llx phys %016llx "
		"%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
		name,
		(unsigned long long)ibVa,
		(unsigned long long)phys,
		(unsigned long)words[0], (unsigned long)words[1],
		(unsigned long)words[2], (unsigned long)words[3],
		(unsigned long)words[4], (unsigned long)words[5],
		(unsigned long)words[6], (unsigned long)words[7]);
}


static void
gpu_stage_dump(const char* name, uint64 ibVa, uint8* cpu, uint64 phys)
{
	uint64 pte = 0;
	if (ibVa != 0)
		r5_gfx9_vmid1_pte(ibVa, &pte);
	r5_gfx9_cp_dump(name, pte);
	gpu_stage_words(name, ibVa, cpu, phys);
}


/* Just below the advertised limits, so a program that grows upward from
 * va_offset or high_va_offset does not land on a stage page. */
static const uint64 kStageLowVram = R5_GPU_VA_MAX - 0x3000ull;
static const uint64 kStageLowGtt = R5_GPU_VA_MAX - 0x2000ull;
static const uint64 kStageHighGtt = R5_GPU_HIGH_VA_MAX - 0x3000ull;
static const uint64 kStageHighVram = R5_GPU_HIGH_VA_MAX - 0x2000ull;
static_assert(kStageLowVram < kStageLowGtt, "stage pages");
static_assert(kStageHighGtt < kStageHighVram, "stage pages");

/* One page per step, above the pages stage P already released. */
static uint64 sStageFresh = 0;


static uint64
gpu_stage_fresh()
{
	if (sStageFresh == 0 || sStageFresh >= R5_GPU_VA_MAX - 0x10000ull)
		sStageFresh = R5_GPU_VA_MAX - 0x50000ull;
	uint64 va = sStageFresh;
	sStageFresh += 0x1000ull;
	return va;
}


struct gpu_stage_page {
	uint64		va;
	uint64		phys;
	uint64		vramOffset;
	uint8*		cpu;
	area_id		area;
	bool		vram;
	bool		gtt;
};


static void
gpu_stage_unbind(gpu_stage_page* page)
{
	if (page == NULL || page->va == 0)
		return;
	r5_gfx9_vmid1_map(page->va, 0);
	r5_gfx9_vmid1_flush();
	page->va = 0;
}


static bool
gpu_stage_bind(gpu_stage_page* page, uint64 va)
{
	uint64 flags = gpu_pte_flags(R5_GPU_VA_READ | R5_GPU_VA_WRITE
		| R5_GPU_VA_EXEC);
	uint64 pte = r5_gfx9_user_pte(page->phys, flags, page->gtt ? 1 : 0, 0,
		&kR5Gfx93Mtype);
	page->va = va;
	if (!r5_gfx9_vmid1_map(va, pte) || !r5_gfx9_vmid1_flush()) {
		page->va = 0;
		return false;
	}
	return true;
}


static void
gpu_stage_release(gpu_stage_page* page)
{
	if (page == NULL || page->va == 0)
		return;
	r5_gfx9_vmid1_map(page->va, 0);
	r5_gfx9_vmid1_flush();
	if (page->vram)
		r5_gfx9_free_user_vram(page->vramOffset);
	if (page->gtt && page->area >= 0)
		delete_area(page->area);
	memset(page, 0, sizeof(*page));
	page->area = -1;
}


static bool
gpu_stage_map(radeon_info* info, gpu_stage_page* page, uint64 va, bool gtt,
	bool wc = false)
{
	memset(page, 0, sizeof(*page));
	page->area = -1;
	page->va = va;
	uint64 phys = 0;
	uint64 flags = gpu_pte_flags(R5_GPU_VA_READ | R5_GPU_VA_WRITE | R5_GPU_VA_EXEC);
	if (!gtt) {
		if (r5_gfx9_alloc_user_vram(4096, &page->vramOffset) != B_OK)
			return false;
		page->cpu = r5_gfx9_vram_cpu(page->vramOffset);
		if (page->cpu == NULL) {
			r5_gfx9_free_user_vram(page->vramOffset);
			return false;
		}
		page->vram = true;
		uint32 fb = gpu_mmio(info, kGcSegment, 0x096b);
		phys = r5_gpu_carveout_base(fb) + page->vramOffset;
	} else {
		void* system = NULL;
		page->area = create_area("r5 gfx9 stage", &system,
			B_ANY_KERNEL_ADDRESS, 4096, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (page->area < 0 || system == NULL)
			return false;
		/* wc is accepted and ignored. A stage GTT page stays cached. */
		(void)wc;
		page->cpu = (uint8*)system;
		page->gtt = true;
		physical_entry entry;
		memset(&entry, 0, sizeof(entry));
		uint32 count = 1;
		status_t mapped = get_memory_map_etc(B_CURRENT_TEAM, system, 4096,
			&entry, &count);
		if (mapped != B_OK || count == 0 || entry.size < 4096) {
			delete_area(page->area);
			page->area = -1;
			return false;
		}
		phys = entry.address;
	}
	page->phys = phys;
	uint64 pte = r5_gfx9_user_pte(phys, flags, gtt ? 1 : 0, 0,
		&kR5Gfx93Mtype);
	if (!r5_gfx9_vmid1_map(va, pte) || !r5_gfx9_vmid1_flush()) {
		gpu_stage_release(page);
		return false;
	}
	return true;
}


struct gpu_stage_pace {
	bool		hang;
	int32		ms;
	uint64		seen;
	uint32		word;
	uint32		cp;
};


/* The stage submit is a job with no sync object, so the retire thread's
 * five-second watchdog is what marks the device lost. */
static int
gpu_stage_slot(uint64 sequence)
{
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (sJobs[i].live)
			continue;
		memset(&sJobs[i], 0, sizeof(sJobs[i]));
		sJobs[i].live = true;
		sJobs[i].sequence = sequence;
		sJobs[i].submitted = system_time();
		return (int)i;
	}
	return -1;
}


static uint32
gpu_stage_word(uint8* destCpu, bool flush)
{
	if (destCpu == NULL)
		return 0;
	if (flush) {
		uintptr_t line = ((uintptr_t)destCpu + 64) & ~(uintptr_t)63;
		asm volatile("clflush (%0)" :: "r"((void*)line) : "memory");
		asm volatile("mfence" ::: "memory");
	}
	return *(volatile uint32*)(destCpu + 64);
}


/* Architectural PAT index: PAT is bit 7, PCD bit 4, PWT bit 3.
 * Write-back is 0. Haiku's write-combining PTE sets only the PAT bit
 * when PAT is on, which is index 4. */
static uint32
gpu_stage_pat(void* cpu)
{
	if (cpu == NULL)
		return 0xffffffffu;
	uint64 va = (uint64)(uintptr_t)cpu;
	uint64 table = 0;
	asm volatile("mov %%cr3, %0" : "=r"(table));
	table &= ~0xfffull;
	static const int shifts[4] = { 39, 30, 21, 12 };
	for (int level = 0; level < 4; level++) {
		void* mapped = NULL;
		area_id id = map_physical_memory("r5 pat", (phys_addr_t)table,
			4096, B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA, &mapped);
		if (id < B_OK || mapped == NULL)
			return 0xffffffffu;
		uint32 slot = (uint32)((va >> shifts[level]) & 0x1ffu);
		uint64 entry = ((volatile uint64*)mapped)[slot];
		delete_area(id);
		if ((entry & 1ull) == 0)
			return 0xffffffffu;
		bool leaf = level == 3 || (entry & (1ull << 7)) != 0;
		if (leaf) {
			uint32 pat = (uint32)((entry >> 7) & 1ull);
			uint32 pcd = (uint32)((entry >> 4) & 1ull);
			uint32 pwt = (uint32)((entry >> 3) & 1ull);
			return (pat << 2) | (pcd << 1) | pwt;
		}
		table = entry & 0x000ffffffffff000ull;
	}
	return 0xffffffffu;
}


/* Caller holds the lock and drops it while polling. A miss is a landed
 * fence, or an idle CP, with the word absent: the series continues.
 * A hang is a busy CP at 5s, or the device already lost. */
static gpu_stage_pace
gpu_stage_pace_wait(radeon_info* info, int slot, uint64 sequence,
	bool expectWord, uint8* destCpu, bool flush, bool requireFence)
{
	bigtime_t start = slot >= 0 ? sJobs[slot].submitted : system_time();
	gpu_stage_pace pace;
	memset(&pace, 0, sizeof(pace));
	gpu_unlock();
	while (true) {
		pace.seen = r5_gfx9_user_seen();
		bool fence = pace.seen == sequence;
		pace.word = expectWord ? gpu_stage_word(destCpu, flush) : 0xDEADBEEF;
		pace.cp = gpu_mmio(info, kGcSegment, 0x01a0);
		bigtime_t elapsed = system_time() - start;
		bool got = pace.word == 0xDEADBEEF;
		if (fence && got) {
			pace.hang = false;
			break;
		}
		if (!requireFence && fence && !got && pace.cp == 0) {
			pace.hang = false;
			break;
		}
		if (sLost || elapsed >= 5000000) {
			if (requireFence)
				pace.hang = sLost || !fence || !got;
			else
				pace.hang = sLost || pace.cp != 0;
			break;
		}
		snooze(1000);
	}
	pace.ms = (int32)((system_time() - start) / 1000);
	gpu_lock();
	if (!pace.hang) {
		if (slot >= 0 && slot < (int)kMaxJob)
			sJobs[slot].live = false;
	} else if (slot >= 0) {
		bigtime_t deadline = system_time() + 2000000;
		while (!sLost && system_time() < deadline) {
			gpu_unlock();
			snooze(1000);
			gpu_lock();
		}
	}
	return pace;
}


static const char*
gpu_stage_how(const gpu_stage_pace& pace, bool expectWord)
{
	if (pace.hang)
		return "hang";
	if (expectWord && pace.word != 0xDEADBEEF)
		return "miss";
	return "ok";
}


/* One VMID 1 IB. The WRITE_DATA destination is destVa. A miss keeps
 * the series going. A hang stops it. linuxWords is the five-dword
 * wb test; the other steps keep the eight-dword packet. */
static bool
gpu_stage_step(radeon_info* info, const char* name, gpu_stage_page* ibPage,
	uint8* destCpu, uint64 destVa, uint64 destPhys, bool expectWord,
	bool ibFlush, bool readFlush, uint32 fill, uint32 dwords, bool linuxWords)
{
	if (ibPage == NULL || ibPage->cpu == NULL)
		return true;
	if (expectWord && destCpu != NULL) {
		*(volatile uint32*)(destCpu + 64) = fill;
		asm volatile("sfence" ::: "memory");
	}
	bool stored = false;
	if (linuxWords)
		stored = r5_gfx9_stage_wb(ibPage->cpu, destVa);
	else if (ibFlush)
		stored = r5_gfx9_stage_ib(ibPage->cpu, destVa);
	else
		stored = r5_gfx9_stage_ib_store(ibPage->cpu, destVa);
	if (!ibFlush)
		asm volatile("sfence" ::: "memory");
	if (!stored) {
		dprintf("radeon_hd: %s ib\n", name);
		return true;
	}
	r5_gfx9_user_ib ib;
	memset(&ib, 0, sizeof(ib));
	ib.va = ibPage->va;
	ib.dwords = dwords;
	ib.vmid = 1;
	sLastIbVa = ibPage->va;
	gpu_stage_words(name, ibPage->va, ibPage->cpu, ibPage->phys);
	uint64 sequence = 0;
	r5_gfx9_set_debug(gpu_submit_log_enabled() ? 1 : 0);
	r5_gfx9_set_job_pasid(0, 0);
	if (!r5_gfx9_user_submit(&ib, 1, 0, &sKernelPreamble, &sequence)) {
		dprintf("radeon_hd: %s submit\n", name);
		gpu_stage_dump(name, ibPage->va, ibPage->cpu, ibPage->phys);
		return !sLost;
	}
	int slot = gpu_stage_slot(sequence);
	gpu_stage_pace pace = gpu_stage_pace_wait(info, slot, sequence, expectWord,
		expectWord ? destCpu : NULL, readFlush, false);
	uint32 pat = destCpu != NULL ? gpu_stage_pat(destCpu + 64) : 0;
	dprintf("radeon_hd: %s %s %ld ms seq %llu seen %llu word %08lx "
		"cp %08lx phys %016llx pat %lu\n",
		name, gpu_stage_how(pace, expectWord), (long)pace.ms,
		(unsigned long long)sequence, (unsigned long long)pace.seen,
		(unsigned long)pace.word, (unsigned long)pace.cp,
		(unsigned long long)destPhys, (unsigned long)pat);
	if (pace.hang) {
		gpu_stage_dump(name, ibPage->va, ibPage->cpu, ibPage->phys);
		gpu_ib_words(name);
	}
	return !pace.hang;
}


static uint64
gpu_stage_rdmsr(uint32 index)
{
	uint32 lo = 0;
	uint32 hi = 0;
	asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(index));
	return ((uint64)hi << 32) | lo;
}


static void
gpu_stage_mtrr(const char* when)
{
	uint64 cap = gpu_stage_rdmsr(0xfeu);
	uint32 count = (uint32)(cap & 0xffu);
	if (count > 16)
		count = 16;
	dprintf("radeon_hd: mtrr %s cap %016llx count %lu\n",
		when, (unsigned long long)cap, (unsigned long)count);
	for (uint32 i = 0; i < count; i++) {
		uint64 base = gpu_stage_rdmsr(0x200u + i * 2u);
		uint64 mask = gpu_stage_rdmsr(0x201u + i * 2u);
		dprintf("radeon_hd: mtrr %s %lu base %016llx mask %016llx\n",
			when, (unsigned long)i, (unsigned long long)base,
			(unsigned long long)mask);
	}
}


/* Eight words through the page's own pointer, then through a second
 * mapping of the same physical page. A mismatch stops the series. */
static bool
gpu_stage_visible(const char* name, gpu_stage_page* page)
{
	uint32 store[8];
	uint32 same[8];
	uint32 other[8];
	if (page == NULL || page->cpu == NULL)
		return false;
	for (uint32 i = 0; i < 8; i++)
		store[i] = 0x51550000u + i;
	memcpy(page->cpu, store, sizeof(store));
	asm volatile("sfence" ::: "memory");
	memcpy(same, page->cpu, sizeof(same));
	memset(other, 0, sizeof(other));
	void* alias = NULL;
	area_id id = map_physical_memory("r5 stage alias",
		(phys_addr_t)page->phys, 4096, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA, &alias);
	bool mapped = id >= B_OK && alias != NULL;
	if (mapped)
		memcpy(other, alias, sizeof(other));
	if (id >= B_OK)
		delete_area(id);
	dprintf("radeon_hd: %s cpu phys %016llx "
		"same %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx "
		"other %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
		name, (unsigned long long)page->phys,
		(unsigned long)same[0], (unsigned long)same[1],
		(unsigned long)same[2], (unsigned long)same[3],
		(unsigned long)same[4], (unsigned long)same[5],
		(unsigned long)same[6], (unsigned long)same[7],
		(unsigned long)other[0], (unsigned long)other[1],
		(unsigned long)other[2], (unsigned long)other[3],
		(unsigned long)other[4], (unsigned long)other[5],
		(unsigned long)other[6], (unsigned long)other[7]);
	bool equal = mapped;
	for (uint32 i = 0; i < 8; i++) {
		if (same[i] != store[i] || other[i] != store[i])
			equal = false;
	}
	if (!equal) {
		dprintf("radeon_hd: %s pat %lu\n", name,
			(unsigned long)gpu_stage_pat(page->cpu));
		gpu_stage_mtrr("mismatch");
	}
	return equal;
}


/* One indirect buffer and one WRITE_DATA page. ibFlush makes the
 * packet visible. readFlush is only the CPU read of the word. */
static bool
gpu_stage_pair(radeon_info* info, const char* name, uint64 ibVa, bool ibGtt,
	bool ibWc, uint64 destVa, bool destGtt, bool destWc, bool ibFlush,
	bool readFlush, uint32 fill, uint32 dwords, bool linuxWords)
{
	gpu_stage_page ib;
	gpu_stage_page dest;
	if (!gpu_stage_map(info, &ib, ibVa, ibGtt, ibWc)) {
		dprintf("radeon_hd: %s map\n", name);
		return true;
	}
	if (!gpu_stage_map(info, &dest, destVa, destGtt, destWc)) {
		dprintf("radeon_hd: %s map\n", name);
		gpu_stage_release(&ib);
		return true;
	}
	if (!gpu_stage_visible(name, &ib) || !gpu_stage_visible(name, &dest)) {
		gpu_stage_release(&dest);
		gpu_stage_release(&ib);
		return false;
	}
	r5_gfx9_vmid1_levels(name, ib.va);
	r5_gfx9_vmid1_levels(name, dest.va);
	bool go = gpu_stage_step(info, name, &ib, dest.cpu, dest.va + 64,
		dest.phys, true, ibFlush, readFlush, fill, dwords, linuxWords);
	gpu_stage_release(&dest);
	gpu_stage_release(&ib);
	return go;
}


/* DMA_DATA from a page in the high range into a fresh low VRAM page.
 * The indirect buffer stays at a fresh low VA. A miss continues. A
 * busy CP at 5s stops the series. */
static bool
gpu_stage_dma(radeon_info* info, const char* name, uint64 srcVa, bool srcGtt)
{
	gpu_stage_page ib;
	gpu_stage_page src;
	gpu_stage_page dest;
	if (!gpu_stage_map(info, &ib, gpu_stage_fresh(), false, false)) {
		dprintf("radeon_hd: %s map\n", name);
		return true;
	}
	if (!gpu_stage_map(info, &src, srcVa, srcGtt, false)) {
		dprintf("radeon_hd: %s map\n", name);
		gpu_stage_release(&ib);
		return true;
	}
	if (!gpu_stage_map(info, &dest, gpu_stage_fresh(), false, false)) {
		dprintf("radeon_hd: %s map\n", name);
		gpu_stage_release(&src);
		gpu_stage_release(&ib);
		return true;
	}
	*(volatile uint32*)(src.cpu + 64) = 0xDEADBEEF;
	asm volatile("sfence" ::: "memory");
	r5_gfx9_stage_bytes(src.cpu, 128);
	uint32* words = (uint32*)ib.cpu;
	/* DMA_DATA control 0x60200000. This is the word the stage ran.
	 * Linux soc15d.h names DST_SEL 0, 1 and 3 and does not name 2.
	 * The checker allows this word and rejects any other DST_SEL 2.
	 * Whether 2 should be 3 waits for its own diagnostic boot. */
	words[0] = 0xc0055000u;
	words[1] = 0x60200000u;
	words[2] = (uint32)src.va;
	words[3] = (uint32)(src.va >> 32);
	words[4] = (uint32)dest.va;
	words[5] = (uint32)(dest.va >> 32);
	words[6] = 0x80000080u;
	r5_gfx9_stage_bytes(ib.cpu, 28);
	r5_gfx9_vmid1_levels(name, src.va);
	r5_gfx9_vmid1_levels(name, dest.va);
	r5_gfx9_user_ib submit;
	memset(&submit, 0, sizeof(submit));
	submit.va = ib.va;
	submit.dwords = 7;
	submit.vmid = 1;
	sLastIbVa = ib.va;
	gpu_stage_words(name, ib.va, ib.cpu, ib.phys);
	uint64 sequence = 0;
	r5_gfx9_set_debug(gpu_submit_log_enabled() ? 1 : 0);
	r5_gfx9_set_job_pasid(0, 0);
	if (!r5_gfx9_user_submit(&submit, 1, 0, &sKernelPreamble, &sequence)) {
		dprintf("radeon_hd: %s submit\n", name);
		gpu_stage_dump(name, ib.va, ib.cpu, ib.phys);
		gpu_stage_release(&dest);
		gpu_stage_release(&src);
		gpu_stage_release(&ib);
		return !sLost;
	}
	int slot = gpu_stage_slot(sequence);
	gpu_stage_pace pace = gpu_stage_pace_wait(info, slot, sequence, true,
		dest.cpu, true, false);
	uint32 pat = gpu_stage_pat(dest.cpu + 64);
	dprintf("radeon_hd: %s %s %ld ms seq %llu seen %llu word %08lx "
		"cp %08lx phys %016llx pat %lu\n",
		name, gpu_stage_how(pace, true), (long)pace.ms,
		(unsigned long long)sequence, (unsigned long long)pace.seen,
		(unsigned long)pace.word, (unsigned long)pace.cp,
		(unsigned long long)dest.phys, (unsigned long)pat);
	if (pace.hang) {
		gpu_stage_dump(name, ib.va, ib.cpu, ib.phys);
		gpu_ib_words(name);
	}
	gpu_stage_release(&dest);
	gpu_stage_release(&src);
	gpu_stage_release(&ib);
	return !pace.hang;
}


static const uint64 kStagePIb = R5_GPU_VA_MAX - 0x8000ull;
static const uint64 kStagePPage = R5_GPU_VA_MAX - 0x7000ull;
static const uint64 kStagePIb2 = R5_GPU_VA_MAX - 0x6000ull;


/* One VMID 1 indirect buffer, one fence. step 0 is the marker
 * self-test and is not a numbered stage P line. ROOM prints the
 * pointers and does not write the packet. */
static uint32
gpu_stage_submit(radeon_info* info, uint32 step, const char* name,
	uint64 va, uint32 n, uint64 extraVa, uint32 extraN)
{
	r5_gfx9_user_ib submit;
	memset(&submit, 0, sizeof(submit));
	submit.va = va;
	submit.dwords = n;
	submit.vmid = 1;
	sLastIbVa = va;
	sLastIbN = extraN != 0 ? 2 : 1;
	sLastIbList[0] = va;
	sLastIbDw[0] = n;
	if (extraN != 0) {
		sLastIbList[1] = extraVa;
		sLastIbDw[1] = extraN;
	}
	uint64 sequence = 0;
	int32 before = r5_gfx9_priv_count();
	uint32 need = r5_gfx9_user_commit_dwords(extraN != 0 ? 2 : 1);
	if (need == 0 || !r5_gfx9_user_room(need)) {
		uint64 wptr = 0;
		uint64 rptr = 0;
		r5_gfx9_ring_ptrs(&wptr, &rptr);
		if (step == 0) {
			dprintf("radeon_hd: mark selftest room wptr %llx rptr %llx\n",
				(unsigned long long)wptr, (unsigned long long)rptr);
		} else {
			dprintf("radeon_hd: p%lu %s room wptr %llx rptr %llx\n",
				(unsigned long)step, name,
				(unsigned long long)wptr, (unsigned long long)rptr);
		}
		if (!sLost)
			gpu_lost(info, "p");
		return R5_GPU_P_ROOM;
	}
	r5_gfx9_set_debug(gpu_submit_log_enabled() ? 1 : 0);
	r5_gfx9_set_job_pasid(0, 0);
	if (!r5_gfx9_user_submit(&submit, 1, 0, &sKernelPreamble, &sequence)) {
		if (step == 0)
			dprintf("radeon_hd: mark selftest submit\n");
		else {
			dprintf("radeon_hd: p%lu %s submit\n",
				(unsigned long)step, name);
		}
		gpu_lost(info, "p");
		return R5_GPU_P_TIMEOUT;
	}
	int slot = gpu_stage_slot(sequence);
	gpu_stage_pace pace = gpu_stage_pace_wait(info, slot, sequence, false,
		NULL, false, true);
	int32 after = r5_gfx9_priv_count();
	uint32 status = R5_GPU_P_OK;
	const char* how = gpu_stage_how(pace, false);
	if (after != before) {
		status = R5_GPU_P_PRIV;
		how = "priv";
		if (!sLost)
			gpu_lost(info, "p");
	} else if (pace.hang) {
		status = R5_GPU_P_TIMEOUT;
		how = "timeout";
		if (!sLost)
			gpu_ib_words(step == 0 ? "mark" : name);
	}
	if (step == 0) {
		dprintf("radeon_hd: mark selftest %s %ld ms\n", how,
			(long)pace.ms);
	} else {
		dprintf("radeon_hd: p%lu %s %s %ld ms\n",
			(unsigned long)step, name, how, (long)pace.ms);
	}
	return status;
}


/* Kernel mappings only. The IB and the test page are VRAM windows,
 * never a user pointer. One packet type per step. A privilege fault
 * marks the device lost. A fence that is still out at 5s is lost
 * through the watchdog. U4 and U2 run later, from stage U. */
static status_t
gpu_stage_p(radeon_info* info, r5_gpu_stage_p* out)
{
	memset(out, 0, sizeof(*out));
	out->size = (uint32)sizeof(*out);
	out->version = 1;
	out->count = R5_P_COUNT;
	for (uint32 i = 0; i < R5_P_COUNT && i < R5_GPU_P_STEPS; i++) {
		const char* name = r5_stage_p_name(i);
		out->status[i] = R5_GPU_P_SKIP;
		for (uint32 c = 0; c < 23 && name[c] != '\0'; c++)
			out->name[i][c] = name[c];
	}
	if (sLost) {
		dprintf("radeon_hd: skipped, gpu lost\n");
		return ECANCELED;
	}
	if (!gpu_render_enabled() || !gpu_selftest_enabled())
		return B_NOT_ALLOWED;
	if (!r5_gfx9_vmid1_setup()) {
		dprintf("radeon_hd: p skip vmid\n");
		return B_NO_INIT;
	}
	gpu_stage_page ib;
	gpu_stage_page ib2;
	gpu_stage_page page;
	memset(&ib, 0, sizeof(ib));
	memset(&ib2, 0, sizeof(ib2));
	memset(&page, 0, sizeof(page));
	if (!gpu_stage_map(info, &ib, kStagePIb, false)
		|| !gpu_stage_map(info, &ib2, kStagePIb2, false)
		|| !gpu_stage_map(info, &page, kStagePPage, false)) {
		dprintf("radeon_hd: p map\n");
		gpu_stage_release(&ib);
		gpu_stage_release(&ib2);
		gpu_stage_release(&page);
		return B_NO_MEMORY;
	}
	memset(page.cpu, 0, 4096);
	*(volatile uint32*)(page.cpu + R5_P_SHADER_OFF) = R5_P_SHADER;
	r5_gfx9_stage_bytes(page.cpu, 4096);
	bool stop = false;
	uint32 probe[8];
	memset(probe, 0, sizeof(probe));
	uint32 probeN = r5_stage_p_build(R5_P_WRITE_DATA, page.va, probe, 8, 0);
	if (probeN == 6)
		probe[4] = R5_P_MARK_VALUE;
	if (probeN != 6) {
		dprintf("radeon_hd: mark selftest bad\n");
		stop = true;
	} else {
		memcpy(ib.cpu, probe, probeN * 4);
		r5_gfx9_stage_bytes(ib.cpu, probeN * 4);
		uint32 probeStatus = gpu_stage_submit(info, 0, "mark", ib.va,
			probeN, 0, 0);
		if (probeStatus != R5_GPU_P_OK) {
			dprintf("radeon_hd: mark selftest bad\n");
			stop = true;
		} else {
			uint32 back = 0;
			uint32 got = r5_gfx9_vmid1_copy(page.va, &back, 1);
			dprintf("radeon_hd: mark selftest %s\n",
				got == 1 && back == R5_P_MARK_VALUE ? "ok" : "bad");
			gpu_mark(page.va);
		}
	}
	for (uint32 i = 0; i < R5_P_COUNT && i < R5_GPU_P_STEPS; i++) {
		if (stop)
			break;
		uint32 words[256];
		memset(words, 0, sizeof(words));
		uint64 extraVa = 0;
		uint32 extraN = 0;
		if (i == R5_P_IB2) {
			uint32 body[8];
			memset(body, 0, sizeof(body));
			extraN = r5_stage_p_ib2_body(page.va, body, 8);
			if (extraN != 0) {
				memcpy(ib2.cpu, body, extraN * 4);
				r5_gfx9_stage_bytes(ib2.cpu, extraN * 4);
				extraVa = ib2.va;
			}
		} else if (i == R5_P_IB24) {
			uint32 preambleN = 0;
			const uint32* preamble = r5_stage_p_preamble(&preambleN);
			memcpy(ib2.cpu, preamble, preambleN * 4);
			r5_gfx9_stage_bytes(ib2.cpu, preambleN * 4);
			extraVa = ib2.va;
			extraN = preambleN;
		}
		uint32 n = r5_stage_p_build(i, page.va, words, 256, ib2.va);
		if (n == 0 || (i == R5_P_IB2 && extraN == 0)) {
			out->status[i] = R5_GPU_P_TIMEOUT;
			out->first_fail = i + 1;
			stop = true;
			continue;
		}
		memcpy(ib.cpu, words, n * 4);
		r5_gfx9_stage_bytes(ib.cpu, n * 4);
		uint32 status = gpu_stage_submit(info, i + 1, r5_stage_p_name(i),
			ib.va, n, extraVa, extraN);
		out->status[i] = status;
		if (status != R5_GPU_P_OK) {
			out->first_fail = i + 1;
			stop = true;
		}
	}
	gpu_stage_release(&page);
	gpu_stage_release(&ib2);
	gpu_stage_release(&ib);
	if (stop)
		dprintf("radeon_hd: skipped, gpu lost\n");
	return stop ? ECANCELED : B_OK;
}


/* Stick 25 does not call this. The CPU self-check stays for a later stick. */
__attribute__((unused))
static bool
gpu_stage_self(radeon_info* info, const char* name, bool gtt)
{
	gpu_stage_page page;
	if (!gpu_stage_map(info, &page, gpu_stage_fresh(), gtt, false)) {
		dprintf("radeon_hd: %s map\n", name);
		return false;
	}
	bool ok = gpu_stage_visible(name, &page);
	gpu_stage_release(&page);
	return ok;
}


/* CPU only. Eight words go out through the PCI BAR. The same offset
 * is read back through the carveout at once, then again after sfence.
 * No clflush: that would hide a write the HDP is still holding. */
static void
gpu_stage_hdp(radeon_info* info)
{
	(void)info;
	uint64 offset = 0;
	uint8* bar = NULL;
	if (r5_gfx9_alloc_vram(4096, 4096, &offset, &bar) != B_OK || bar == NULL) {
		dprintf("radeon_hd: hdp alloc fail\n");
		return;
	}
	uint8* alias = r5_gfx9_vram_cpu(offset);
	if (alias == NULL || alias == bar || r5_gfx9_vram_bar(offset) != bar) {
		dprintf("radeon_hd: hdp alias fail off %016llx\n",
			(unsigned long long)offset);
		return;
	}
	volatile uint32* src = (volatile uint32*)(bar + 64);
	volatile uint32* dst = (volatile uint32*)(alias + 64);
	for (uint32 i = 0; i < 8; i++)
		src[i] = 0xA1000000u + i;
	uint32 now[8];
	uint32 later[8];
	for (uint32 i = 0; i < 8; i++)
		now[i] = dst[i];
	asm volatile("sfence" ::: "memory");
	for (uint32 i = 0; i < 8; i++)
		later[i] = dst[i];
	dprintf("radeon_hd: hdp off %016llx now %08x %08x %08x %08x "
		"%08x %08x %08x %08x\n",
		(unsigned long long)offset,
		now[0], now[1], now[2], now[3], now[4], now[5], now[6], now[7]);
	dprintf("radeon_hd: hdp sfence %08x %08x %08x %08x %08x %08x %08x %08x\n",
		later[0], later[1], later[2], later[3],
		later[4], later[5], later[6], later[7]);
}


static uint32
gpu_stage_r1_word(gpu_stage_page* page)
{
	uint32 word = 0;
	if (page != NULL && page->cpu != NULL)
		memcpy(&word, page->cpu, sizeof(word));
	return word;
}


static uint32
gpu_stage_r1_job(radeon_info* info, const char* name, gpu_stage_page* ib,
	const uint32* words, uint32 n)
{
	memcpy(ib->cpu, words, n * 4);
	r5_gfx9_stage_bytes(ib->cpu, n * 4);
	return gpu_stage_submit(info, 1, name, ib->va, n, 0, 0);
}


static void
gpu_stage_r1_line(const char* what, uint64 x, uint64 y)
{
	dprintf("radeon_hd: t %lld %s x %016llx y %016llx "
		"inv %d hub %d vmids %d fence %d cond %d gds %d pasid %d ce %d\n",
		(long long)system_time(), what == NULL ? "" : what,
		(unsigned long long)x, (unsigned long long)y,
		r5_gfx9_inv_range_on(), r5_gfx9_hub_linux_on(),
		r5_gfx9_vmids_on(), r5_gfx9_flush_fence_on(),
		r5_gfx9_cond_exec_on(), r5_gfx9_gds_switch_on(),
		r5_gfx9_pasid_map_on(), r5_gfx9_ce_meta_on());
}


/* R1a WRITE_DATA and R1b RELEASE_MEM, or R1c the IB fetch.
 * Page A stays allocated until this returns, so a stale write still
 * has a page to land on. R1c is a separate call: a miss there can hang. */
static void
gpu_stage_r1(radeon_info* info, uint32 which, uint64 x, uint64 y,
	uint32_t* oldWord, uint32_t* newWord)
{
	gpu_stage_page a;
	gpu_stage_page b;
	gpu_stage_page cmd;
	gpu_stage_page dest;
	uint32 words[8];
	uint32 n;
	char what[80];
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	memset(&cmd, 0, sizeof(cmd));
	memset(&dest, 0, sizeof(dest));
	if (oldWord != NULL)
		*oldWord = 0;
	if (newWord != NULL)
		*newWord = 0;
	if (!gpu_stage_map(info, &a, x, true)
		|| !gpu_stage_map(info, &cmd, gpu_stage_fresh(), false)
		|| !gpu_stage_map(info, &b, gpu_stage_fresh(), false)
		|| !gpu_stage_map(info, &dest, y, false)) {
		gpu_stage_r1_line("r1 map", x, y);
		gpu_stage_release(&dest);
		gpu_stage_release(&b);
		gpu_stage_release(&cmd);
		gpu_stage_release(&a);
		return;
	}
	gpu_stage_unbind(&b);
	memset(a.cpu, 0, 4096);
	memset(b.cpu, 0, 4096);
	memset(dest.cpu, 0, 4096);
	if (which != R5_GPU_R1_AB)
		goto r1c;
	n = r5_r1_write(words, 8, x, 0x11111111u);
	gpu_stage_r1_line("r1a gtt", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1a-gtt", &cmd, words, n)
			!= R5_GPU_P_OK) {
		gpu_stage_r1_line("r1a gtt job", x, y);
		goto out;
	}
	gpu_stage_unbind(&a);
	if (!gpu_stage_bind(&b, x))
		goto out;
	n = r5_r1_write(words, 8, x, 0x22222222u);
	gpu_stage_r1_line("r1a vram", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1a-vram", &cmd, words, n)
			!= R5_GPU_P_OK)
		goto out;
	snprintf(what, sizeof(what), "r1a gtt %08lx vram %08lx",
		(unsigned long)gpu_stage_r1_word(&a),
		(unsigned long)gpu_stage_r1_word(&b));
	gpu_stage_r1_line(what, x, y);
	if (oldWord != NULL)
		*oldWord = gpu_stage_r1_word(&a);
	if (newWord != NULL)
		*newWord = gpu_stage_r1_word(&b);

	gpu_stage_unbind(&b);
	if (!gpu_stage_bind(&a, x))
		goto out;
	memset(a.cpu, 0, 4);
	memset(b.cpu, 0, 4);
	n = r5_r1_release(words, 8, x, 0x11111111u);
	gpu_stage_r1_line("r1b gtt", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1b-gtt", &cmd, words, n)
			!= R5_GPU_P_OK)
		goto out;
	gpu_stage_unbind(&a);
	if (!gpu_stage_bind(&b, x))
		goto out;
	n = r5_r1_release(words, 8, x, 0x22222222u);
	gpu_stage_r1_line("r1b vram", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1b-vram", &cmd, words, n)
			!= R5_GPU_P_OK)
		goto out;
	snprintf(what, sizeof(what), "r1b gtt %08lx vram %08lx",
		(unsigned long)gpu_stage_r1_word(&a),
		(unsigned long)gpu_stage_r1_word(&b));
	gpu_stage_r1_line(what, x, y);
	goto out;

r1c:
	if (which != R5_GPU_R1_C)
		goto out;
	gpu_stage_r1_line("r1c before", x, y);
	gpu_stage_unbind(&b);
	if (!gpu_stage_bind(&a, x))
		goto out;
	n = r5_r1_write(words, 8, y, 0x33333333u);
	gpu_stage_r1_line("r1c gtt", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1c-gtt", &a, words, n)
			!= R5_GPU_P_OK)
		goto out;
	gpu_stage_unbind(&a);
	if (!gpu_stage_bind(&b, x))
		goto out;
	n = r5_r1_write(words, 8, y, 0x44444444u);
	memcpy(b.cpu, words, n * 4);
	r5_gfx9_stage_bytes(b.cpu, n * 4);
	gpu_stage_r1_line("r1c vram", x, y);
	if (gpu_stage_r1_job(info, "r1c-vram", &b, words, n) != R5_GPU_P_OK)
		goto out;
	snprintf(what, sizeof(what), "r1c dest %08lx",
		(unsigned long)gpu_stage_r1_word(&dest));
	gpu_stage_r1_line(what, x, y);
out:
	gpu_stage_release(&dest);
	gpu_stage_release(&b);
	gpu_stage_release(&cmd);
	gpu_stage_r1_line("r1 free a", x, y);
	gpu_stage_release(&a);
}


static status_t
gpu_stage_u(radeon_info* info)
{
	if (!gpu_render_enabled() || !gpu_selftest_enabled())
		return B_NOT_ALLOWED;
	sStageFresh = 0;
	gpu_stage_mtrr("before");
	gpu_stage_hdp(info);
	if (!r5_gfx9_vmid1_setup()) {
		dprintf("radeon_hd: u1 skip vmid\n");
		return B_NO_INIT;
	}
	bool stop = false;
	/* The HDP probe ran first and does not use the GPU. Group B then
	 * writes only, with the indirect buffer at a fresh low address.
	 * Group A fetches a VRAM indirect buffer. C1 is last. */
	if (!stop && !gpu_stage_pair(info, "b1", gpu_stage_fresh(), false,
			false, 0xffff800000041000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "b2", gpu_stage_fresh(), false,
			false, 0xffff800000041000ull, true, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "b3", gpu_stage_fresh(), false,
			false, 0xffff800000001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_dma(info, "b5", 0xffff800000041000ull, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a2", 0xffff800100000000ull, false,
			false, 0xffff800100001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a1", R5_GPU_HIGH_VA_MAX - 0x1000ull, false,
			false, R5_GPU_HIGH_VA_MAX - 0x2000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a3", 0xffff808000000000ull, false,
			false, 0xffff808000001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a8", 0x01000000ull, false,
			false, 0x01001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a7", 0x00100000ull, false,
			false, 0x00101000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a4", 0xffff8000c0000000ull, false,
			false, 0xffff8000c0001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a5", 0xffff800040000000ull, false,
			false, 0xffff800040001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a6", 0xffff800000200000ull, false,
			false, 0xffff800000201000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "c1",
			R5_GPU_HIGH_VA_OFFSET + 0x1000ull, false, false,
			R5_GPU_HIGH_VA_OFFSET + 0x2000ull, false, false,
			true, true, 0, 8, false))
		stop = true;
	gpu_stage_mtrr("after");
	return B_OK;
}








static status_t
gpu_debug_switch(void* buffer, size_t length)
{
	r5_gpu_debug_switch in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	if (!gpu_debug_enabled())
		return B_NOT_ALLOWED;
	if ((in.which & R5_GPU_SWITCH_VMID_PER_FILE) != 0)
		return B_NOT_ALLOWED;
	if ((in.which & R5_GPU_SWITCH_CE_META) != 0) {
		dprintf("radeon_hd: gfx9_ce_meta is a boot setting\n");
		return B_NOT_ALLOWED;
	}
	/* Idle first. The flush is after the registers move. */
	bigtime_t deadline = system_time() + 5000000;
	while (r5_gfx9_user_seen() < sHighestSequence) {
		if (system_time() > deadline)
			return B_TIMED_OUT;
		snooze(1000);
	}
	r5_gfx9_switch_registers(
		(in.which & R5_GPU_SWITCH_INV_RANGE) != 0 ? (in.on ? 1 : 0) : -1,
		(in.which & R5_GPU_SWITCH_HUB_LINUX) != 0 ? (in.on ? 1 : 0) : -1,
		(in.which & R5_GPU_SWITCH_VMIDS) != 0 ? (in.on ? 1 : 0) : -1);
	if ((in.which & R5_GPU_SWITCH_FLUSH_FENCE) != 0)
		r5_gfx9_set_flush_fence(in.on ? 1 : 0);
	if ((in.which & R5_GPU_SWITCH_COND_EXEC) != 0)
		r5_gfx9_set_cond_exec(in.on ? 1 : 0);
	if ((in.which & R5_GPU_SWITCH_GDS) != 0)
		r5_gfx9_set_gds_switch(in.on ? 1 : 0);
	if ((in.which & R5_GPU_SWITCH_DEFERRED) != 0) {
		sDeferred = in.on ? 1 : 0;
		r5_gfx9_set_defer_stamp(sDeferred ? gpu_defer_stamp : 0);
	}
	if (!r5_gfx9_vmid1_flush())
		return B_ERROR;
	dprintf("radeon_hd: t %lld switch which %lu on %lu "
		"inv %d hub %d vmids %d fence %d cond %d gds %d defer %d pasid %d\n",
		(long long)system_time(), (unsigned long)in.which,
		(unsigned long)in.on, r5_gfx9_inv_range_on(),
		r5_gfx9_hub_linux_on(), r5_gfx9_vmids_on(),
		r5_gfx9_flush_fence_on(), r5_gfx9_cond_exec_on(),
		r5_gfx9_gds_switch_on(), sDeferred, r5_gfx9_pasid_map_on());
	return B_OK;
}



static void*
amd_device(void)
{
	return gDeviceInfo[0];
}


static int
amd_read_reg(void* info, uint32 offset, uint32* value)
{
	if (value == NULL)
		return B_BAD_VALUE;
	if (offset != kGbAddrConfig)
		return B_NOT_ALLOWED;
	*value = gpu_mmio((radeon_info*)info, kGcSegment, offset);
	return B_OK;
}


static int
amd_vram_bytes(void* info, uint64* bytes)
{
	radeon_info* device = (radeon_info*)info;
	if (bytes == NULL || device == NULL || device->shared_info == NULL)
		return B_ERROR;
	*bytes = (uint64)device->shared_info->frame_buffer_size * 1024ull;
	return B_OK;
}


static int
amd_vram_phys(void* info, uint64 byteOffset, uint64* phys)
{
	if (phys == NULL)
		return B_BAD_VALUE;
	uint32 fb = gpu_mmio((radeon_info*)info, kGcSegment, 0x096b) & 0x00ffffffu;
	*phys = ((uint64)fb << 24) + byteOffset;
	return B_OK;
}


static int
amd_vram_alloc(uint64 bytes, uint64* offset)
{
	return r5_gfx9_alloc_user_vram(bytes, offset);
}


static void
amd_vram_free(uint64 offset)
{
	r5_gfx9_free_user_vram(offset);
}


static uint64
amd_encode_pte(uint64 physical, uint32 flags, int system)
{
	return r5_gfx9_user_pte(physical, gpu_pte_flags(flags), system, 0,
		&kR5Gfx93Mtype);
}


static int
amd_vm_map(void* vm, uint64 va, uint64 pte)
{
	return r5_gfx9_vm_map((r5_vm*)vm, va, pte) ? 1 : 0;
}


static int
amd_vm_lookup(void* vm, uint64 va)
{
	return r5_gfx9_vm_lookup((r5_vm*)vm, va) ? 1 : 0;
}


static int
amd_map_prt(void* vm, uint64 va)
{
	return r5_gfx9_vm_map((r5_vm*)vm, va, kR5Gfx9PtePrt) ? 1 : 0;
}


static int
amd_vm_flush(void)
{
	return r5_gfx9_vmid1_flush() ? 1 : 0;
}


static int
amd_vmid_prepare(void)
{
	return r5_gfx9_vmid1_setup() ? 1 : 0;
}


static int
amd_vmid_per_file(void)
{
	return r5_gfx9_vmid_per_file() ? 1 : 0;
}


static void*
amd_shared_vm(void)
{
	return r5_gfx9_shared_vm();
}


static int
amd_file_vm_init(void* vm, void* pages, uint32 pagesN)
{
	return r5_gfx9_file_vm_init((r5_vm*)vm, (r5_vm_page*)pages, pagesN) ? 1 : 0;
}


static uint32
amd_commit_dwords(uint32 count, int fileTree, uint32 pasid)
{
	uint32 dwords = r5_gfx9_user_commit_dwords(count);
	/* The file's page-table base is written after the room wait, so the
	 * count includes one VM flush that touch() will make due. */
	if (fileTree && dwords != 0) {
		dwords += r5_cs_vm_switch_words();
		if (pasid != 0
			&& (r5_gfx9_pasid_map_on() || r5_gfx9_vmid_per_file()))
			dwords += 5u;
	}
	return dwords;
}


static int
amd_user_room(uint32 dwords)
{
	return r5_gfx9_user_room(dwords) ? 1 : 0;
}


static uint64
amd_user_seen(void)
{
	return r5_gfx9_user_seen();
}


static void
amd_set_debug(int on)
{
	r5_gfx9_set_debug(on);
}


static void
amd_set_job_pasid(uint32 pasid, uint64 client)
{
	r5_gfx9_set_job_pasid(pasid, client);
}


static void
amd_set_file_root(uint64 physical)
{
	r5_gfx9_vmid1_set_root(physical | (uint64)R5_GFX9_PTE_VALID);
}


static void
amd_set_shared_root(void)
{
	r5_gfx9_vmid1_set_root(r5_gfx9_vmid1_shared_root());
}


static void
amd_touch_root(void)
{
	r5_gfx9_vmid1_touch();
}


static int
amd_user_submit(const uint64* va, const uint32* dwords, const uint32* vmid,
	const uint32* flags, uint32 count, uint64 fenceCtx, int32* preamble,
	uint64* sequence)
{
	r5_gfx9_user_ib ibs[kMaxList];
	uint32 i;
	if (count > kMaxList || va == NULL || dwords == NULL || vmid == NULL
		|| flags == NULL || preamble == NULL || sequence == NULL)
		return 0;
	for (i = 0; i < count; i++) {
		ibs[i].va = va[i];
		ibs[i].dwords = dwords[i];
		ibs[i].vmid = vmid[i];
		ibs[i].flags = flags[i];
	}
	return r5_gfx9_user_submit(ibs, count, fenceCtx, preamble, sequence) ? 1 : 0;
}


static int
amd_fault_take(uint32* pasid, uint32* vmid, uint64* addr)
{
	return r5_gfx9_fault_take(pasid, vmid, addr);
}


static void
amd_ring_waited(void)
{
	r5_gfx9_ring_waited();
}


static void
amd_ring_fill(uint32* waits, uint32* maxFill, uint32* dwords)
{
	r5_gfx9_ring_fill(waits, maxFill, dwords);
}


static int
amd_map_vram(uint64 offset, uint64 bytes, void** address)
{
	return r5_gfx9_map_vram(offset, bytes, address);
}


static int
amd_get_info(void* client, void* buffer, size_t length)
{
	return gpu_get_info((gpu_client*)client, buffer, length);
}


static int
amd_debug_switch(void* buffer, size_t length)
{
	return gpu_debug_switch(buffer, length);
}


static int
amd_stage_p(void* info, void* out)
{
	return gpu_stage_p((radeon_info*)info, (r5_gpu_stage_p*)out);
}


static int
amd_stage_u(void* info)
{
	return gpu_stage_u((radeon_info*)info);
}


static int
amd_stage_r1(void* info, uint32 which, uint64 x, uint64 y, uint32* oldWord,
	uint32* newWord)
{
	gpu_stage_r1((radeon_info*)info, which, x, y, oldWord, newWord);
	return B_OK;
}


static int
amd_log_state(const char* why, char* line, uint32 bytes)
{
	r5_gfx9_log_state(why, line, bytes);
	return B_OK;
}


static int
amd_gtt_wc(void)
{
	return r5_gfx9_wc_gtt() ? 1 : 0;
}


static int
amd_dump(const char* why)
{
	if (why == NULL)
		gpu_dump_priv();
	else
		gpu_ib_words(why);
	return B_OK;
}


static int amd_scanout(void* info, uint32* width, uint32* height,
	uint32* bytesPerRow, uint32* format, uint64* vramOffset, uint64* bytes);


r5_gpu_backend_ops gR5GpuOps = {
	amd_device,
	amd_read_reg,
	amd_vram_bytes,
	amd_vram_phys,
	amd_vram_alloc,
	amd_vram_free,
	amd_encode_pte,
	amd_vm_map,
	amd_vm_lookup,
	amd_map_prt,
	amd_vm_flush,
	amd_vmid_prepare,
	amd_vmid_per_file,
	amd_shared_vm,
	amd_file_vm_init,
	amd_commit_dwords,
	amd_user_room,
	amd_user_seen,
	amd_set_debug,
	amd_set_job_pasid,
	amd_set_file_root,
	amd_set_shared_root,
	amd_touch_root,
	amd_user_submit,
	amd_get_info,
	amd_debug_switch,
	amd_stage_p,
	amd_stage_u,
	amd_stage_r1,
	amd_log_state,
	amd_dump,
	amd_gtt_wc,
	amd_fault_take,
	amd_map_vram,
	amd_ring_waited,
	amd_ring_fill,
	amd_scanout
};


static int
amd_scanout(void* info, uint32* width, uint32* height, uint32* bytesPerRow,
	uint32* format, uint64* vramOffset, uint64* bytes)
{
	radeon_info* device = (radeon_info*)info;
	if (device == NULL || device->shared_info == NULL || width == NULL
		|| height == NULL || bytesPerRow == NULL || format == NULL
		|| vramOffset == NULL || bytes == NULL)
		return B_ERROR;
	radeon_shared_info* shared = device->shared_info;
	uint32 w = shared->current_mode.virtual_width;
	uint32 h = shared->current_mode.virtual_height;
	uint32 pitch = shared->bytes_per_row;
	if (w == 0 || h == 0 || pitch < w * 4u || (pitch & 3u) != 0) {
		dprintf("radeon_hd: scanout refused mode %lu x %lu pitch %lu\n",
			(unsigned long)w, (unsigned long)h, (unsigned long)pitch);
		return B_ERROR;
	}
	uint64 picture = (uint64)pitch * h;
	uint64 mapped = (picture + 4095ull) & ~4095ull;
	/* The CPU sees the firmware picture through the PCI BAR. The GPU
	 * page tables add MC_VM_FB_OFFSET << 24 to an offset in that BAR.
	 * On the 5625U the BAR is 0xfce0000000 and the carveout is
	 * 0x810000000. Subtracting those from each other is not an offset. */
	uint64 bar = shared->vram_bar_phys;
	uint64 window = shared->vram_bar_bytes;
	uint64 phys = (uint64)shared->frame_buffer_phys;
	if (bar == 0 || window == 0 || phys < bar || (phys & 4095ull) != 0
		|| phys - bar > window || mapped > window - (phys - bar)) {
		dprintf("radeon_hd: scanout refused phys 0x%llx bar 0x%llx"
			" size 0x%llx pitch %lu mode %lu x %lu\n",
			(unsigned long long)phys, (unsigned long long)bar,
			(unsigned long long)window, (unsigned long)pitch,
			(unsigned long)w, (unsigned long)h);
		return B_ERROR;
	}
	*width = w;
	*height = h;
	*bytesPerRow = pitch;
	*format = (uint32)shared->current_mode.space;
	*vramOffset = phys - bar;
	*bytes = mapped;
	dprintf("radeon_hd: scanout bar+0x%llx %lu x %lu pitch %lu\n",
		(unsigned long long)(phys - bar), (unsigned long)w,
		(unsigned long)h, (unsigned long)pitch);
	return B_OK;
}
