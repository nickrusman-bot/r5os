/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
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
	const uint32_t seg1 = GC_BASE__INST0_SEG1;
	const uint32_t contextStart = 0x0000a000u;
	const uint32_t contextEnd = 0x0000a400u;
	const uint32_t uconfigStart = 0x0000c000u;
	const uint32_t preambleBegin = 2u << 28;
	const uint32_t preambleEnd = 3u << 28;
	const uint32_t uconfigIndex = 2u << 28;
	const uint32_t vgtIndex = 0x2243u;
	uint32_t n = 0;
	uint32_t vgt;
	uint32_t scratch;

	ring[n++] = (3u << 30) | (0x4Au << 8);
	ring[n++] = preambleBegin;
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
	ring[n++] = 3u;
	ring[n++] = 0x8000u;
	ring[n++] = 0x8000u;
	ring[n++] = (3u << 30) | (1u << 16) | (0x79u << 8);
	ring[n++] = uconfigIndex | vgt;
	ring[n++] = 0;
	ring[n++] = (3u << 30) | (1u << 16) | (0x79u << 8);
	ring[n++] = scratch;
	ring[n++] = 0xDEADBEEFu;
	if (n != 914u)
		return 0;
	return n;
}


/* r5-gpu-isolation.cpp fill_ib. Memory to memory, then WRITE_DATA. */
static void
isolation(uint32_t* words, uint64_t dest, uint64_t readBack)
{
	words[0] = 0xc0044000u;
	words[1] = 1u | (5u << 8) | (1u << 16) | (1u << 20);
	words[2] = (uint32_t)dest;
	words[3] = (uint32_t)(dest >> 32);
	words[4] = (uint32_t)readBack;
	words[5] = (uint32_t)(readBack >> 32);
	words[6] = 0xc0033700u;
	words[7] = (5u << 8) | (1u << 20);
	words[8] = (uint32_t)dest;
	words[9] = (uint32_t)(dest >> 32);
	words[10] = 0xdeadbeefu;
	words[11] = 0xffff1000u;
	words[12] = 0xffff1000u;
	words[13] = 0xffff1000u;
}


/* r5-gpu-selftest.cpp, the eight-dword indirect buffer. */
static void
selftest_ib(uint32_t* packet, uint64_t word)
{
	packet[0] = (3u << 30) | (3u << 16) | (0x37u << 8);
	packet[1] = (5u << 8) | (1u << 20);
	packet[2] = (uint32_t)word;
	packet[3] = (uint32_t)(word >> 32);
	packet[4] = 0xDEADBEEFu;
	packet[5] = (3u << 30) | (0x3fffu << 16) | (0x10u << 8);
	packet[6] = packet[5];
	packet[7] = packet[5];
}


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

	n = kernel_ring(ring, 1024);
	if (n != 914u)
		return 1;
	begin("kernel-ring");
	dump(ring, n);

	isolation(words, 0x80000000ull, 0x80000100ull);
	begin("isolation");
	dump(words, 14);

	selftest_ib(words, 0x80001040ull);
	begin("selftest-ib");
	dump(words, 8);

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

	return 0;
}
