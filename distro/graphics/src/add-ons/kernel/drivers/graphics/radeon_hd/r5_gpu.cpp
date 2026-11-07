/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AMD backend for the GPU core. Registers, page tables, the ring and
 * the stage programs live here. The core calls them through gR5GpuOps.
 */

#include "r5_gpu_internal.h"
#include "radeon_hd_private.h"
#include "gfx9_start.h"
#include "r5_gpu.h"
#include "r5_span.h"
#include "r5_wait.h"
#include "r5_stage_p.h"
#include "r5_log.h"
#include "r5_vm.h"

#include <KernelExport.h>

#include <stdlib.h>
#include <string.h>


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
static r5_amd_device sPassport;
static bool sPassportReady = false;
static void* stage_as()
{
	static void* as = NULL;
	if (as == NULL) {
		if (!r5_gfx9_vmid1_setup())
			return NULL;
		as = r5_gfx9_as_open();
	}
	return as;
}

static bool gpu_ring(void* as, r5_gfx9_user_ib* ibs, uint32 count,
	uint64* sequence);


extern "C" status_t vm_set_area_memory_type(area_id id,
	phys_addr_t physicalBase, uint32 type);


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
gpu_active_cus(radeon_info* info, uint32* _bitmap)
{
	uint32 cc = gpu_mmio(info, kGcSegment, kShaderArrayConfig);
	uint32 user = gpu_mmio(info, kGcSegment, kUserShaderArrayConfig);
	const r5_amd_row* row = info->pci == NULL ? NULL
		: r5_amd_row_find(info->pci->device_id);
	uint32 width = row == NULL || row->gen == NULL ? 0
		: row->gen->num_cu_per_sh;
	uint32 count = 0;
	uint32 bitmap = r5_amd_cu_bitmap(cc, user, width, &count);
	if (_bitmap != NULL)
		*_bitmap = bitmap;
	if (gpu_debug_enabled())
	dprintf("radeon_hd: gfx9 cu cc %08lx user %08lx active %lu bitmap %08lx\n",
		(unsigned long)cc, (unsigned long)user, (unsigned long)count,
		(unsigned long)bitmap);
	return count;
}


/* gfx_v9_0_get_rb_active_bitmap and gfx_v9_0_setup_rb. One SE and one
 * SH, so this does not write GRBM_GFX_INDEX. num is the pipe count
 * Linux puts in num_rb_pipes (max backends times shader engines).
 * mask is backend_enable_mask. */
static void
gpu_rb(radeon_info* info, uint32* num, uint32* mask)
{
	uint32 cc = gpu_mmio(info, kGcSegment, kRbBackendDisable);
	uint32 user = gpu_mmio(info, kGcSegment, kUserRbBackendDisable);
	const r5_amd_row* row = info->pci == NULL ? NULL
		: r5_amd_row_find(info->pci->device_id);
	uint32 backends = row == NULL || row->gen == NULL ? 0
		: row->gen->max_backends_per_se;
	uint32 engines = row == NULL || row->gen == NULL ? 0
		: row->gen->num_shader_engines;
	uint32 active = r5_amd_rb_mask(cc, user, backends);
	uint32 pipes = backends * engines;
	if (num != NULL)
		*num = pipes;
	if (mask != NULL)
		*mask = active;
	if (r5_log_allow(0, 0, R5_LOG_ONCE, &sErrorLines, &sOnceBits, R5_ONCE_RB))
	dprintf("radeon_hd: rb num %lu mask %lx cc %08lx user %08lx\n",
		(unsigned long)pipes, (unsigned long)active,
		(unsigned long)cc, (unsigned long)user);
}


void
r5_gpu_fill_passport(radeon_info* info)
{
	if (info == NULL || info->pci == NULL)
		return;
	uint32 rbCc = gpu_mmio(info, kGcSegment, kRbBackendDisable);
	uint32 rbUser = gpu_mmio(info, kGcSegment, kUserRbBackendDisable);
	uint32 cuCc = gpu_mmio(info, kGcSegment, kShaderArrayConfig);
	uint32 cuUser = gpu_mmio(info, kGcSegment, kUserShaderArrayConfig);
	gpu_active_cus(info, NULL);
	gpu_rb(info, NULL, NULL);
	r5_amd_device_fill(&sPassport, info->pci->device_id, gpu_strap_rev(info),
		info->pci->revision, rbCc, rbUser, cuCc, cuUser,
		info->fw_version, info->fw_feature);
	if (info->shared_info != NULL && info->shared_info->frame_buffer != NULL) {
		uint64 vramBytes = (uint64)info->shared_info->frame_buffer_size * 1024;
		if (vramBytes > R5_DISCOVERY_TMR_OFFSET
			&& vramBytes - R5_DISCOVERY_TMR_OFFSET >= R5_DISCOVERY_TMR_SIZE) {
			static uint8 sDiscovery[R5_DISCOVERY_TMR_SIZE];
			const uint8* src = (const uint8*)info->shared_info->frame_buffer
				+ (vramBytes - R5_DISCOVERY_TMR_OFFSET);
			char mismatch[160];
			memcpy(sDiscovery, src, R5_DISCOVERY_TMR_SIZE);
			if (r5_amd_discovery_apply(&sPassport, sDiscovery,
					R5_DISCOVERY_TMR_SIZE, mismatch, (int)sizeof(mismatch))
				&& mismatch[0] != 0)
				dprintf("radeon_hd: %s\n", mismatch);
		}
	}
	sPassportReady = true;
	if (r5_log_allow(0, 0, R5_LOG_ONCE, &sErrorLines, &sOnceBits,
			R5_ONCE_PASSPORT)) {
		char text[4096];
		r5_amd_passport_format(&sPassport, text, (int)sizeof(text));
		dprintf("radeon_hd: passport\n%s", text);
	}
}


static status_t
gpu_wait_room(radeon_info* info, uint32 dwords)
{
	(void)info;
	if (sLost)
		return ECANCELED;
	if (r5_gfx9_user_room(dwords))
		return B_OK;
	bigtime_t deadline = system_time() + (bigtime_t)kRoomWaitUs;
	while (!r5_gfx9_user_room(dwords)) {
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		bigtime_t now = system_time();
		if (r5_gfx9_user_room(dwords))
			return B_OK;
		if (sLost)
			return ECANCELED;
		if (now >= deadline)
			return B_BUSY;
		gpu_unlock();
		entry.Wait(B_RELATIVE_TIMEOUT, deadline - now);
		gpu_lock();
	}
	return B_OK;
}


static uint32
gpu_known_dw(uint64 va)
{
	for (uint32 i = 0; i < sLastIbN && i < 8; i++) {
		if (sLastIbList[i] == va && sLastIbDw[i] != 0)
			return sLastIbDw[i];
	}
	return 0;
}


static void gpu_dump_one(const char* why, const char* which, uint64 base,
	uint32 hint, uint32 cpOffset, int depth, void* as);


static void
gpu_mark(uint64 va)
{
	uint32 word = 0;
	uint32 got = 0;
	uint64 pte = 0;
	if (va != 0) {
		got = r5_gfx9_vmid1_copy(stage_as(), va, &word, 1);
		r5_gfx9_vmid1_pte(stage_as(), va, &pte);
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


static bool
gpu_dump_begin()
{
	return r5_log_allow(0, 0, R5_LOG_DUMP, &sErrorLines, &sOnceBits, -1) != 0;
}


static bool
gpu_dump_line()
{
	return r5_log_allow(0, 0, R5_LOG_DUMP, &sErrorLines, &sOnceBits, 0) != 0;
}


static void
gpu_pm4_walk(const char* why, const uint32* words, uint32 count, int depth,
	void* as)
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
			gpu_dump_one(why, "nested", child, childDw, 0, 1, as);
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
	uint32 cpOffset, int depth, void* as)
{
	uint32 words[1024];
	uint32 want = hint;
	memset(words, 0, sizeof(words));
	if (want == 0 || want > 1024)
		want = 1024;
	uint32 got = 0;
	if (base != 0)
		got = r5_gfx9_vmid1_copy(as, base, words, want);
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
	gpu_pm4_walk(why, words, got, depth, as);
}


static void
gpu_dump_regs(const char* why, const r5_gfx9_ib_now* ib, const char* which,
	void* as)
{
	if (ib == NULL || ib->base == 0)
		return;
	uint32 known = gpu_known_dw(ib->base);
	uint32 hint = known != 0 ? known : 1024;
	gpu_dump_one(why, which, ib->base, hint, ib->offset, 0, as);
}


static void
gpu_ib_words(const char* why, void* as)
{
	r5_gfx9_ib_now ib1;
	r5_gfx9_ib_now ib2;
	memset(&ib1, 0, sizeof(ib1));
	memset(&ib2, 0, sizeof(ib2));
	r5_gfx9_ib_regs(&ib1, &ib2);
	gpu_dump_regs(why, &ib1, "ib1", as);
	if (ib2.base != ib1.base)
		gpu_dump_regs(why, &ib2, "ib2", as);
	if (ib1.base == 0 && ib2.base == 0 && !gpu_dump_begin())
		return;
	if (gpu_dump_line())
	dprintf("radeon_hd: %s roq ib1 %08lx\n",
		why == NULL ? "ib" : why, (unsigned long)r5_gfx9_roq_ib1());
	if (sInfo != NULL) {
		uint64 pte = 0;
		uint64 va = ib1.base != 0 ? ib1.base : sLastIbVa;
		if (va != 0)
			r5_gfx9_vmid1_pte(as, va, &pte);
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
	gpu_dump_regs("ih", &ib1, "ib1", NULL);
	if (ib2.base != ib1.base)
		gpu_dump_regs("ih", &ib2, "ib2", NULL);
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
		r5_gfx9_vmid1_pte(stage_as(), ibVa, &pte);
	r5_gfx9_cp_dump(name, pte);
	gpu_stage_words(name, ibVa, cpu, phys);
}


/* Just below the advertised limits, so a program that grows upward from
 * va_offset or high_va_offset does not land on a stage page. */
static const uint64 kStageLowGtt = R5_GPU_VA_MAX - 0x2000ull;
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
gpu_stage_release(gpu_stage_page* page)
{
	if (page == NULL || page->va == 0)
		return;
	gR5GpuOps.vm_map(stage_as(), page->va, 0);
	gR5GpuOps.tlb_flush(stage_as());
	if (page->vram)
		gR5GpuOps.vram_free(page->vramOffset);
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
	uint32 flags = R5_GPU_VA_READ | R5_GPU_VA_WRITE | R5_GPU_VA_EXEC;
	if (!gtt) {
		if (gR5GpuOps.vram_alloc(4096, &page->vramOffset) != B_OK)
			return false;
		radeon_shared_info* shared = info->shared_info;
		if (info->kernel_window == NULL || shared == NULL
			|| page->vramOffset < shared->vram_pool_offset) {
			gR5GpuOps.vram_free(page->vramOffset);
			return false;
		}
		page->cpu = info->kernel_window
			+ (page->vramOffset - shared->vram_pool_offset);
		page->vram = true;
		uint32 fb = gpu_mmio(info, kGcSegment, 0x096b) & 0x00ffffffu;
		phys = ((uint64)fb << 24) + page->vramOffset;
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
	uint64 pte = 0;
	if (stage_as() == NULL
		|| gR5GpuOps.encode_pte(phys, flags, gtt ? 1 : 0, 0, &pte)
			!= B_OK
		|| gR5GpuOps.vm_map(stage_as(), va, pte) != B_OK
		|| gR5GpuOps.tlb_flush(stage_as()) != B_OK) {
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


/* Caller holds the lock and drops it while polling. A miss is a landed
 * fence with the word absent: the series continues. A hang is a busy
 * CP at 5s, or the device already lost. Stage P still requires the fence. */
static gpu_stage_pace
gpu_stage_pace_wait(radeon_info* info, int slot, uint64 sequence,
	bool expectWord, uint8* destCpu, bool flush, bool requireFence)
{
	bigtime_t start = slot >= 0 ? sJobs[slot].submitted : system_time();
	gpu_stage_pace pace;
	memset(&pace, 0, sizeof(pace));
	gpu_unlock();
	while (true) {
		uint64 seen = 0;
		gR5GpuOps.poll_retired(&seen);
		pace.seen = seen;
		bool fence = seen == sequence;
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


/* One indirect buffer on the VMID this file holds. A new owner flushes
 * that VMID before the doorbell. The wait does not mark the GPU lost.
 * The watchdog does, at 5s. */
static bool
gpu_ring(void* as, r5_gfx9_user_ib* ibs, uint32 count, uint64* sequence)
{
	uint32 vmid = 0;
	int flush = 0;
	uint64 pd = 0;
	uint32 need;
	if (as == NULL || ibs == NULL || count == 0)
		return false;
	if (!r5_gfx9_as_reserve(as, &vmid, &flush, &pd))
		return false;
	need = r5_gfx9_user_commit_switch(count, flush);
	if (need == 0 || !r5_gfx9_user_room(need)) {
		r5_gfx9_as_cancel();
		return false;
	}
	for (uint32 i = 0; i < count; i++)
		ibs[i].vmid = vmid;
	if (!r5_gfx9_user_submit_switch(ibs, count, sequence, flush, pd,
			r5_gfx9_as_pasid(as), r5_gfx9_as_first(as))) {
		r5_gfx9_as_cancel();
		return false;
	}
	r5_gfx9_as_finish(as, vmid, sequence != NULL ? *sequence : 0);
	return true;
}


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


/* The WRITE_DATA destination is destVa. A miss keeps the series going.
 * A hang stops it. linuxWords is the five-dword wb test. */
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
	sLastIbVa = ibPage->va;
	gpu_stage_words(name, ibPage->va, ibPage->cpu, ibPage->phys);
	uint64 sequence = 0;
	if (!gpu_ring(stage_as(), &ib, 1, &sequence)) {
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
		gpu_ib_words(name, stage_as());
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


static bool
gpu_stage_one(radeon_info* info, const char* name, uint64 va, bool gtt)
{
	gpu_stage_page page;
	if (!gpu_stage_map(info, &page, va, gtt)) {
		dprintf("radeon_hd: %s map\n", name);
		return true;
	}
	if (!gpu_stage_visible(name, &page)) {
		gpu_stage_release(&page);
		return false;
	}
	r5_gfx9_vmid1_levels(name, va);
	r5_gfx9_vmid1_levels(name, va + 0x1000ull);
	bool go = gpu_stage_step(info, name, &page, page.cpu, va + 64, page.phys,
		true, true, true, 0, 8, false);
	gpu_stage_release(&page);
	return go;
}


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
	if (!gpu_ring(stage_as(), &submit, 1, &sequence)) {
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
			gpu_ib_words(step == 0 ? "mark" : name, stage_as());
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
	if (gR5GpuOps.vmid_prepare() != B_OK) {
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
			uint32 got = r5_gfx9_vmid1_copy(stage_as(), page.va, &back, 1);
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


static status_t
gpu_stage_u(radeon_info* info)
{
	if (!gpu_render_enabled() || !gpu_selftest_enabled())
		return B_NOT_ALLOWED;
	if (gR5GpuOps.vmid_prepare() != B_OK) {
		dprintf("radeon_hd: u1 skip vmid\n");
		return B_NO_INIT;
	}
	sStageFresh = 0;
	gpu_stage_mtrr("before");
	bool stop = false;
	if (!gpu_stage_self(info, "u-self", true)
		|| !gpu_stage_self(info, "u-self", false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "u2a-n", gpu_stage_fresh(), false,
			false, gpu_stage_fresh(), true, false, true, false, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "u2a-f", gpu_stage_fresh(), false,
			false, gpu_stage_fresh(), true, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "u2b-n", gpu_stage_fresh(), true,
			false, gpu_stage_fresh(), false, false, false, false, 0, 8, false))
		stop = true;
	/* u4b-pair maps the next page as well. The single-page steps below
	 * leave that neighbour as the Linux empty PTE. */
	if (!stop && !gpu_stage_pair(info, "u4b-pair",
			R5_GPU_HIGH_VA_OFFSET + 0x1000ull, false, false,
			R5_GPU_HIGH_VA_OFFSET + 0x2000ull, false, false,
			true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_one(info, "u-top", 0xffffffffffbfc000ull, false))
		stop = true;
	if (!stop && !gpu_stage_one(info, "u-radv", 0xffff800100000000ull, false))
		stop = true;
	if (!stop && !gpu_stage_one(info, "u-257", 0xffff808000000000ull, false))
		stop = true;
	if (!stop && !gpu_stage_one(info, "u-pdb1", 0xffff800040000000ull, false))
		stop = true;
	if (!stop && !gpu_stage_one(info, "u-pdb0", 0xffff800000200000ull, false))
		stop = true;
	if (!stop && !gpu_stage_one(info, "u-low0", 0x100000ull, false))
		stop = true;
	if (!stop && !gpu_stage_one(info, "u4b",
			R5_GPU_HIGH_VA_OFFSET + 0x1000ull, false))
		stop = true;
	gpu_stage_mtrr("after");
	return B_OK;
}


static int
amd_vendor_size(uint32_t* bytes, uint32_t* version)
{
	if (bytes != NULL)
		*bytes = (uint32_t)sizeof(r5_gpu_amd_info);
	if (version != NULL)
		*version = 1;
	return B_OK;
}


static int
amd_fill_vendor_info(void* info, void* block, uint32_t bytes)
{
	if (info == NULL || block == NULL || bytes < sizeof(r5_gpu_amd_info))
		return B_BAD_VALUE;
	if (!sPassportReady)
		r5_gpu_fill_passport((radeon_info*)info);
	r5_gpu_amd_info amd;
	memset(&amd, 0, sizeof(amd));
	amd.size = (uint32)sizeof(amd);
	amd.version = 1;
	amd.device.device_id = sPassport.device_id;
	amd.device.chip_rev = sPassport.chip_rev;
	amd.device.external_rev = sPassport.external_rev;
	amd.device.pci_rev = sPassport.pci_rev;
	amd.device.family = sPassport.family;
	amd.device.num_shader_engines = sPassport.num_shader_engines;
	amd.device.num_shader_arrays_per_engine = sPassport.num_shader_arrays_per_engine;
	amd.device.cu_active_number = sPassport.cu_active_number;
	amd.device.cu_bitmap[0][0] = sPassport.cu_bitmap;
	amd.device.num_cu_per_sh = sPassport.num_cu_per_sh;
	amd.device.ids_flags = sPassport.ids_flags;
	amd.device.num_rb_pipes = sPassport.num_rb_pipes;
	amd.device.enabled_rb_pipes_mask = sPassport.enabled_rb_pipes_mask;
	amd.device.num_hw_gfx_contexts = sPassport.num_hw_gfx_contexts;
	amd.device.ce_ram_size = sPassport.ce_ram_size;
	amd.device.pte_fragment_size = sPassport.pte_fragment_size;
	amd.device.max_engine_clock = sPassport.max_engine_clock;
	amd.device.min_engine_clock = sPassport.min_engine_clock;
	amd.device.max_memory_clock = sPassport.max_memory_clock;
	amd.device.min_memory_clock = sPassport.min_memory_clock;
	amd.device.vram_type = sPassport.vram_type;
	amd.device.vram_bit_width = sPassport.vram_bit_width;
	amd.device.gc_double_offchip_lds_buf = sPassport.gc_double_offchip_lds_buf;
	amd.device.num_shader_visible_vgprs = sPassport.num_shader_visible_vgprs;
	amd.device.num_tcc_blocks = sPassport.num_tcc_blocks;
	amd.device.gs_vgt_table_depth = sPassport.gs_vgt_table_depth;
	amd.device.gs_prim_buffer_depth = sPassport.gs_prim_buffer_depth;
	amd.device.max_gs_waves_per_vgt = sPassport.max_gs_waves_per_vgt;
	amd.device.gpu_counter_freq = sPassport.gpu_counter_freq;
	amd.device.virtual_address_offset = R5_GPU_VA_OFFSET;
	amd.device.virtual_address_max = R5_GPU_VA_MAX;
	amd.device.virtual_address_alignment = sPassport.page_size;
	amd.device.gart_page_size = sPassport.page_size;
	amd.device.wave_front_size = sPassport.wave_front_size;
	amd.device.high_va_offset = R5_GPU_HIGH_VA_OFFSET;
	amd.device.high_va_max = R5_GPU_HIGH_VA_MAX;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_major = sPassport.hw_major;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_minor = sPassport.hw_minor;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].ib_start_alignment = sPassport.ib_start_alignment;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].ib_size_alignment = sPassport.ib_size_alignment;
	/* Linux v6.12 one GFX ring. Compute stays off. */
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].available_rings = 1;
	amd.hw_ip[R5_AMDGPU_HW_IP_COMPUTE] = amd.hw_ip[R5_AMDGPU_HW_IP_GFX];
	amd.hw_ip[R5_AMDGPU_HW_IP_COMPUTE].available_rings = 0;
	for (uint32 i = 0; i < R5_AMDGPU_FW_COUNT && i < 32; i++) {
		amd.firmware[i].ver = sPassport.fw_ver[i];
		amd.firmware[i].feature = sPassport.fw_feature[i];
	}
	memcpy(block, &amd, sizeof(amd));
	return B_OK;
}


static int
amd_read_reg(void* info, uint32_t offset, uint32_t* value)
{
	if (value == NULL)
		return B_BAD_VALUE;
	if (offset == R5_GPU_QUERY_VRAM_BASE) {
		/* mmMC_VM_FB_LOCATION. The mapper uses the low 24 bits << 24. */
		*value = gpu_mmio((radeon_info*)info, kGcSegment, 0x096b) & 0x00ffffffu;
		return B_OK;
	}
	if (offset != kGbAddrConfig)
		return B_NOT_ALLOWED;
	*value = gpu_mmio((radeon_info*)info, kGcSegment, offset);
	return B_OK;
}


static int
amd_vram_alloc(uint64_t bytes, uint64_t* offset)
{
	return r5_gfx9_alloc_user_vram(bytes, offset);
}


static int
amd_vram_free(uint64_t offset)
{
	r5_gfx9_free_user_vram(offset);
	return B_OK;
}


static int
amd_set_cpu_type(int area, uint64_t physical, uint32_t type)
{
	return vm_set_area_memory_type((area_id)area, physical, type);
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


static int
amd_encode_pte(uint64_t physical, uint32_t flags, int system, int writeCombine,
	uint64_t* pte)
{
	if (pte == NULL)
		return B_BAD_VALUE;
	*pte = r5_gfx9_user_pte(physical, gpu_pte_flags(flags), system, writeCombine,
		&kR5Gfx93Mtype);
	return B_OK;
}


static int
amd_vm_map(void* as, uint64_t va, uint64_t pte)
{
	return r5_gfx9_vmid1_map(as, va, pte) ? B_OK : B_ERROR;
}


static int
amd_vm_unmap(void* as, uint64_t va, uint64_t bytes)
{
	for (uint64_t page = 0; page < bytes; page += 4096) {
		if (!r5_gfx9_vmid1_map(as, va + page, 0))
			return B_ERROR;
	}
	return B_OK;
}


static int
amd_vm_prt(void* as, uint64_t va, uint64_t bytes)
{
	for (uint64_t page = 0; page < bytes; page += 4096) {
		if (!r5_gfx9_vmid1_map(as, va + page, kR5Gfx9PtePrt))
			return B_ERROR;
	}
	return B_OK;
}


static int
amd_tlb_flush(void* as)
{
	uint32 vmid;
	if (as == NULL)
		return B_BAD_VALUE;
	vmid = r5_gfx9_as_vmid(as);
	if (vmid == 0)
		return B_OK;
	return r5_gfx9_tlb_flush_vmid(vmid) ? B_OK : B_ERROR;
}


static int
amd_vmid_prepare(void)
{
	return r5_gfx9_vmid1_setup() ? B_OK : B_ERROR;
}


static int
amd_vmid_lookup(void* as, uint64_t va)
{
	/* B_OK means the page is mapped. B_ENTRY_NOT_FOUND means it is not. */
	return r5_gfx9_vmid1_lookup(as, va) ? B_OK : B_ENTRY_NOT_FOUND;
}


static int
amd_space_lost(void* as)
{
	/* ECANCELED means this file's address space took a GC hub fault.
	 * Another file still returns B_OK. */
	return r5_gfx9_as_lost(as) ? ECANCELED : B_OK;
}


static int
amd_wait_room(uint32_t count)
{
	uint32 need = r5_gfx9_user_commit_switch(count, 1);
	if (need == 0)
		return B_ERROR;
	return gpu_wait_room(sInfo, need);
}


static int
amd_submit(void* as, const uint64_t* va, const uint32_t* dwords, uint32_t count,
	uint64_t* sequence)
{
	uint32 vmid = 0;
	int flush = 0;
	uint64 pd = 0;
	if (as == NULL || count > 8 || va == NULL || dwords == NULL
		|| sequence == NULL)
		return B_BAD_VALUE;
	/* Room was waited for, and the kernel copy flushed, before this
	 * call. Reserve, write, and ring while the core still holds the
	 * lock. SH_MEM is written by the VMID 1 setup for VMIDs 1-15. */
	if (!r5_gfx9_as_reserve(as, &vmid, &flush, &pd))
		return B_BUSY;
	r5_gfx9_user_ib ibs[8];
	for (uint32_t i = 0; i < count; i++) {
		ibs[i].va = va[i];
		ibs[i].dwords = dwords[i];
		ibs[i].vmid = vmid;
	}
	if (!r5_gfx9_user_submit_switch(ibs, count, sequence, flush, pd,
			r5_gfx9_as_pasid(as), r5_gfx9_as_first(as))) {
		r5_gfx9_as_cancel();
		return B_ERROR;
	}
	r5_gfx9_as_finish(as, vmid, *sequence);
	return B_OK;
}


static int
amd_poll_retired(uint64_t* seen)
{
	if (seen == NULL)
		return B_BAD_VALUE;
	*seen = r5_gfx9_user_seen();
	return B_OK;
}



static int
amd_sh_mem(uint32_t vmid)
{
	/* The K path does not call this. r5_gfx9_sh_mem writes the same
	 * registers as the VMID 1..15 loop and then restores VMID 0. */
	return r5_gfx9_sh_mem(vmid) ? B_OK : B_BAD_VALUE;
}


static int
amd_ih_decode(const uint32_t* words, uint32_t count, uint32_t* source,
	uint64_t* address)
{
	/* Linux v6.12 amdgpu_ih_decode_iv_helper: source is bits 15:8 of
	 * dword 0, and src_data starts at dword 4. gmc_v9_0_process_interrupt
	 * turns those two dwords into a byte address the same way as
	 * r5_gfx9_fault_address. The interrupt handler does not call this. */
	if (words == NULL || source == NULL || address == NULL || count < 8)
		return B_BAD_VALUE;
	uint8 raw[32];
	memcpy(raw, words, 32);
	r5_gfx9_ih_entry entry = r5_gfx9_ih_parse(raw);
	*source = entry.source;
	*address = r5_gfx9_fault_address(words[4], words[5]);
	return B_OK;
}


static int
amd_fault_decode(uint32_t addrLo, uint32_t addrHi, uint64_t* address)
{
	if (address == NULL)
		return B_BAD_VALUE;
	*address = r5_gfx9_fault_address(addrLo, addrHi);
	return B_OK;
}


static int
amd_dump(const char* why, void* as)
{
	/* A null reason is the retire thread's privilege snapshot.
	 * A reason is the indirect-buffer dump the refusal path already prints.
	 * as is that client's address space. */
	if (why == NULL)
		gpu_dump_priv();
	else
		gpu_ib_words(why, as);
	return B_OK;
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
amd_address_open(void** as)
{
	if (as == NULL)
		return B_BAD_VALUE;
	if (!r5_gfx9_vmid1_setup())
		return B_ERROR;
	*as = r5_gfx9_as_open();
	return *as == NULL ? B_NO_MEMORY : B_OK;
}


static int
amd_address_close(void* as, uint64_t seen)
{
	r5_gfx9_as_close(as, seen);
	return B_OK;
}


static int
amd_bind(void* as)
{
	r5_gfx9_as_bind(as);
	return B_OK;
}


static int
amd_limits(uint64_t* vramCap, uint64_t* gttCap)
{
	return r5_gfx9_mem_limits(vramCap, gttCap) ? B_OK : B_ERROR;
}


static int
amd_note_retired(uint64_t seen)
{
	r5_gfx9_as_retired(seen);
	return B_OK;
}


static int
amd_space_hold(void* as)
{
	r5_gfx9_as_hold(as);
	return B_OK;
}


static int
amd_space_drop(void* as)
{
	r5_gfx9_as_drop(as);
	return B_OK;
}


static int
amd_space_counts(void* as, uint32_t* ptPages, uint32_t* vmid,
	uint32_t* ptPagesAll, uint32_t* vmidsAll)
{
	r5_gfx9_as_counts(as, ptPages, vmid, ptPagesAll, vmidsAll);
	return B_OK;
}


r5_gpu_backend_ops gR5GpuOps = {
	.fill_vendor_info = amd_fill_vendor_info,
	.read_reg = amd_read_reg,
	.vram_alloc = amd_vram_alloc,
	.vram_free = amd_vram_free,
	.set_cpu_type = amd_set_cpu_type,
	.encode_pte = amd_encode_pte,
	.vm_map = amd_vm_map,
	.vm_unmap = amd_vm_unmap,
	.vm_prt = amd_vm_prt,
	.tlb_flush = amd_tlb_flush,
	.vmid_prepare = amd_vmid_prepare,
	.vmid_lookup = amd_vmid_lookup,
	.sh_mem = amd_sh_mem,
	.submit = amd_submit,
	.poll_retired = amd_poll_retired,
	.ih_decode = amd_ih_decode,
	.fault_decode = amd_fault_decode,
	.dump = amd_dump,
	.stage_p = amd_stage_p,
	.stage_u = amd_stage_u,
	.address_open = amd_address_open,
	.address_close = amd_address_close,
	.bind = amd_bind,
	.limits = amd_limits,
	.note_retired = amd_note_retired,
	.wait_room = amd_wait_room,
	.space_lost = amd_space_lost,
	.space_hold = amd_space_hold,
	.space_drop = amd_space_drop,
	.space_counts = amd_space_counts,
	.vendor_size = amd_vendor_size
};
