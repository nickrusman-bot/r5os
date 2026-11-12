/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Builds the hand-written PM4 streams on the host and prints them for
 * scripts/check-pm4.py. The words come from the same headers the kernel
 * and the tests compile. Addresses are stand-ins so the control fields
 * are the real ones.
 */

#include "clearstate_defs.h"
#include "clearstate_gfx9.h"
#include "gfx9_compute_ib.h"
#include "r5_cs.h"
#include "r5_gc9_regs.h"
#include "r5_isolation_ib.h"
#include "r5_pm4_workload.h"
#include "r5_selftest_ib.h"
#include "r5_stage_p.h"
#include "r5_stage_r1.h"

#include <stdio.h>
#include <stdint.h>


static void
begin(const char* name)
{
	printf("# %s\n", name);
}


static void
dump(const uint32_t* words, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
		printf("%08x%s", words[i], (i + 1u == count) ? "\n" : " ");
	printf("\n");
}


/* gfx9_start.cpp radeon_gfx9_fill. The clear-state table is
 * clearstate_gfx9.h. 914 dwords is the ring contract. */
static uint32_t
kernel_ring(uint32_t* ring, uint32_t capacity)
{
	const uint32_t kPreambleBegin = 2u << 28;
	const uint32_t kUconfigIndexType = 2u << 28;
	const uint32_t kCePartitionBase = 3;
	const uint32_t kGfx9StartDwords = 914;
	const uint32_t seg1 = GC_BASE__INST0_SEG1;
	const uint32_t contextStart = 0x0000a000u;
	const uint32_t contextEnd = 0x0000a400u;
	const uint32_t uconfigStart = 0x0000c000u;
	const uint32_t preambleEnd = 3u << 28;
	const uint32_t vgtIndex = 0x2243u;
	uint32_t n = 0;
	uint32_t vgt;
	uint32_t scratch;

	ring[n++] = (3u << 30) | (0x4Au << 8);
	ring[n++] = kPreambleBegin;
	ring[n++] = (3u << 30) | (1u << 16) | (0x28u << 8);
	ring[n++] = 0x80000000u;
	ring[n++] = 0x80000000u;
	for (const cs_section_def* sect = gfx9_cs_data; sect->section != NULL;
		++sect) {
		for (const cs_extent_def* ext = sect->section; ext->extent != NULL;
			++ext) {
			if (sect->id != SECT_CONTEXT)
				return 0;
			if (ext->reg_count == 0 || ext->reg_index < contextStart
				|| ext->reg_index > contextEnd - ext->reg_count)
				return 0;
			if (n + 2u + ext->reg_count > capacity)
				return 0;
			ring[n++] = (3u << 30) | (ext->reg_count << 16) | (0x69u << 8);
			ring[n++] = ext->reg_index - contextStart;
			for (unsigned i = 0; i < ext->reg_count; i++)
				ring[n++] = ext->extent[i];
		}
	}
	vgt = (seg1 + vgtIndex) - uconfigStart;
	scratch = (seg1 + mmSCRATCH_REG0) - uconfigStart;
	if (vgt >= 0x400u || scratch >= 0x400u)
		return 0;
	if (n + 16u > capacity)
		return 0;
	ring[n++] = (3u << 30) | (0x4Au << 8);
	ring[n++] = preambleEnd;
	ring[n++] = (3u << 30) | (0x12u << 8);
	ring[n++] = 0;
	ring[n++] = (3u << 30) | (2u << 16) | (0x11u << 8);
	ring[n++] = kCePartitionBase;
	ring[n++] = 0x8000u;
	ring[n++] = 0x8000u;
	ring[n++] = (3u << 30) | (1u << 16) | (0x79u << 8);
	ring[n++] = kUconfigIndexType | vgt;
	ring[n++] = 0;
	ring[n++] = (3u << 30) | (1u << 16) | (0x79u << 8);
	ring[n++] = scratch;
	ring[n++] = 0xDEADBEEFu;
	if (n != kGfx9StartDwords)
		return 0;
	return n;
}


/* One site per packet builder. The key is file, function, and the
 * order of packet lines in that function. check-pm4.py compares this
 * list with its own scan. A comment above a packet does not move it. */
static const char* kSites[] = {
	"distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp:gpu_stage_dma#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_fill#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_fill#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_fill#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_fill#4",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_fill#5",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_fill#6",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_fill#7",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_fill#8",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_stage_ib_store#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_stage_wb#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_user_submit#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_user_submit#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_user_submit#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_dma_ib#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_dma_ib#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_dma_ib#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_dma#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_dma#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_workload#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_workload#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_workload#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_compute#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_compute#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:radeon_gfx9_compute#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_k1_submit#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_k1_submit#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_k1_submit#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_k2_dma#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_k2_dma#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_k2_gart_ib#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_k2_gart_ib#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp:r5_gfx9_k2_gart_ib#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_ce_meta#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_user_body#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_user_body#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_user_body#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_user_body#4",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_user_body#5",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_user_body#6",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_user_body#7",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_user_body#8",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_user_body#9",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_eop_fence#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_flush_fence#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_flush_fence#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_cond_exec#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_write_reg#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_pad#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_pasid_map#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_vm_switch#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_vm_switch#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_vm_switch#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_vm_switch#4",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h:r5_cs_vm_switch#5",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#4",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#5",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#6",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#7",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#8",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#9",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#10",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#11",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#12",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#13",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#14",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#15",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#16",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#17",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#18",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#19",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#20",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#21",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#22",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#23",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Preamble#24",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Context4#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Context4#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5Context4#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5ShPrefix#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5ShPrefix#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5ShPrefix#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:kR5ShPrefix#4",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_ib2_body#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#3",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#4",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#5",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#6",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#7",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#8",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#9",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#10",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#11",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#12",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#13",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#14",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#15",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#16",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#17",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#18",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#19",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#20",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#21",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#22",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#23",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#24",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#25",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#26",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#27",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#28",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#29",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#30",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#31",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#32",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#33",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#34",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#35",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#36",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h:r5_stage_p_build#37",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_r1.h:r5_r1_write#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h:r5_acquire_mem#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h:r5_set_uconfig#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h:r5_set_sh#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h:r5_release_counter#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h:r5_gfx9_ib_compute#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h:r5_gfx9_ib_compute#2",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h:r5_gfx9_submit#1",
	"distro/graphics/src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h:r5_gfx9_submit#2",
	"distro/graphics/src/bin/r5-gpu-isolation/r5_isolation_ib.h:r5_isolation_fill_ib#1",
	"distro/graphics/src/bin/r5-gpu-isolation/r5_isolation_ib.h:r5_isolation_fill_ib#2",
	"distro/graphics/src/bin/r5-gpu-selftest/r5_selftest_ib.h:r5_selftest_fill_ib#1",
	"distro/graphics/src/bin/r5-gpu-selftest/r5_selftest_ib.h:r5_selftest_fill_ib#2",
	0
};


int
main()
{
	uint32_t ring[1024];
	uint32_t words[256];
	uint32_t n;
	uint64_t page = 0x80001000ull;
	uint64_t ibs[1] = { 0x80002000ull };
	uint32_t dwords[1] = { 8u };
	uint32_t vmids[1] = { 1u };
	struct r5_gds job;
	uint32_t i;

	for (uint32_t s = 0; kSites[s] != 0; s++)
		printf("# site %s\n", kSites[s]);

	n = kernel_ring(ring, 1024);
	if (n != 914u)
		return 1;
	begin("kernel-ring");
	dump(ring, n);

	r5_isolation_fill_ib(words, 0x80000000ull, 0x80000100ull);
	begin("isolation");
	dump(words, 14);

	r5_selftest_fill_ib(words, 0x80001040ull);
	begin("selftest-ib");
	dump(words, 8);

	r5_pm4_stage_ib_store(words, 0x80001040ull);
	begin("stage-ib-store");
	dump(words, 8);
	r5_pm4_stage_wb(words, 0x80001040ull);
	begin("stage-wb");
	dump(words, 5);
	n = r5_pm4_dma_ib(words, 0x80000000ull, 0x90000000ull);
	begin("dma-ib");
	dump(words, n);
	r5_pm4_submit(words, 0x80002000ull, n, 0x80000440ull, 2ull, 1u << 29, 0);
	begin("workload-submit");
	dump(words, 12);
	r5_pm4_submit(words, 0x80002000ull, 5u, 0x80000440ull, 3ull, 1u << 29, 2u << 24);
	begin("user-submit");
	dump(words, 12);
	r5_pm4_switch_pair(words);
	begin("switch-pair");
	dump(words, 4);
	r5_pm4_stage_dma(words, 0x80003000ull, 0x80004000ull);
	begin("stage-dma");
	dump(words, 7);

	for (i = 0; i < R5_P_COUNT; i++) {
		char name[32];
		n = r5_stage_p_build(i, page, words, 256, 0x80003000ull);
		if (n == 0)
			return 2;
		snprintf(name, sizeof(name), "stage-p-%u", i);
		begin(name);
		dump(words, n);
	}

	n = r5_r1_write(words, 32, page, 0x11111111u);
	if (n == 0)
		return 3;
	begin("r1-write");
	dump(words, n);
	n = r5_r1_release(words, 32, page, 1u);
	if (n == 0)
		return 3;
	begin("r1-release");
	dump(words, n);

	n = r5_gfx9_ib_compute(words, 0x80004000ull, 0x41u, 0x8cu, 0x80005000ull,
		1u, 0x80006000ull, 0x80006010ull);
	if (n == 0 || n > 256)
		return 4;
	begin("compute-ib");
	dump(words, n);
	n = r5_gfx9_submit(words, 0x80002000ull, 73u, 0x80007000ull, 3u);
	begin("compute-submit");
	dump(words, n);

	n = r5_cs_user_body(words, 256, ibs, dwords, vmids, 1, 0x66d000ull, 3ull,
		0x401040ull, kR5CsCntxKeep, 0, 0);
	if (n == 0)
		return 5;
	begin("user-body");
	dump(words, n);

	n = r5_cs_vm_switch(words, 64, 1u, 0x12c18001ull, 0x66d000ull, 2ull);
	if (n == 0)
		return 6;
	begin("vm-switch");
	dump(words, n);

	n = r5_cs_cond_exec(words, 16, 0, 0x401080ull);
	begin("cond-exec");
	dump(words, n);

	n = r5_cs_ce_meta(words, 32, 0, 0xffffffffffe00000ull);
	begin("ce-meta");
	dump(words, n);

	n = r5_cs_pasid_map(words, 16, 0, 1u, 0x123u);
	begin("pasid-map");
	dump(words, n);

	job.gds_base = 0;
	job.gds_size = 0;
	job.gws_base = 0;
	job.gws_size = 0;
	job.oa_base = 0;
	job.oa_size = 0;
	job.valid = 1;
	n = r5_cs_gds_switch(words, 64, 0, 1u, &job);
	if (n == 0)
		return 7;
	begin("gds-switch");
	dump(words, n);

	n = r5_cs_write_reg(words, 16, 0, 0x2000u, 0x7u);
	begin("write-reg");
	dump(words, n);

	r5_cs_pad(words, 4);
	begin("pad-4");
	dump(words, 4);

	n = r5_cs_eop_fence(words, 64, 0, 0x80001000ull, 4ull);
	if (n == 0)
		return 8;
	begin("eop-fence");
	dump(words, n);
	n = r5_cs_flush_fence(words, 64, 0, 0x80001000ull, 5ull);
	if (n == 0)
		return 8;
	begin("flush-fence");
	dump(words, n);

	return 0;
}
