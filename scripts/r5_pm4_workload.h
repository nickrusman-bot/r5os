/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copies of the GFX9 workload packets for the host checker. The kernel
 * ring in gfx9_start.cpp is not built here. scripts/check-pm4.py compares
 * these initializers with the ones in that file, so a drift fails the
 * check. Do not change a word to make the checker pass.
 */
#ifndef R5_PM4_WORKLOAD_H
#define R5_PM4_WORKLOAD_H

#include <stdint.h>


static const uint32_t kWlWriteData = 0x37;
static const uint32_t kWlIndirectBuffer = 0x3f;
static const uint32_t kWlReleaseMem = 0x49;
static const uint32_t kWlWriteDstMemory = 5u << 8;
static const uint32_t kWlWrConfirm = 1u << 20;
static const uint32_t kWlIbValid = 1u << 23;
static const uint32_t kWlCacheFlushTs = 0x14;
static const uint32_t kWlDmaData = 0x50;
static const uint32_t kWlDmaCpSync = 1u << 31;
static const uint32_t kWlDmaSrcData = 2u << 29;
static const uint32_t kWlDmaRawWait = 1u << 30;
static const uint32_t kWlDataSelCounter = 3u << 29;
static const uint32_t kFenceIbWord = 1024;
static const uint32_t kFenceSequence = 1088;
static const uint32_t kFenceTsStart = 1152;
static const uint32_t kFenceTsEnd = 1216;
static const uint32_t kDmaChunk = 0x100000;
static const uint32_t kWlSubmitDwords = 12;
static const uint32_t kK2Pattern = 0xA5A5A5A5;
/* CP_SYNC, source select 0, destination select 0. Stick 43. */
static const uint32_t kStageDmaCtrl = 0x80000000u;


static inline uint32_t
r5_pm4_packet3(uint32_t opcode, uint32_t count)
{
	return (3u << 30) | ((count & 0x3fffu) << 16) | ((opcode & 0xffu) << 8);
}


/* r5_gfx9_stage_ib_store. */
static inline void
r5_pm4_stage_ib_store(uint32_t* ib, uint64_t destVa)
{
	ib[0] = r5_pm4_packet3(kWlWriteData, 3);
	ib[1] = kWlWriteDstMemory | kWlWrConfirm;
	ib[2] = (uint32_t)destVa;
	ib[3] = (uint32_t)(destVa >> 32);
	ib[4] = 0xDEADBEEF;
	ib[5] = 0xFFFF1000;
	ib[6] = 0xFFFF1000;
	ib[7] = 0xFFFF1000;
}


/* r5_gfx9_stage_wb. */
static inline void
r5_pm4_stage_wb(uint32_t* ib, uint64_t destVa)
{
	ib[0] = r5_pm4_packet3(kWlWriteData, 3);
	ib[1] = kWlWriteDstMemory | kWlWrConfirm;
	ib[2] = (uint32_t)destVa;
	ib[3] = (uint32_t)(destVa >> 32);
	ib[4] = 0xDEADBEEF;
}


/* radeon_gfx9_dma_ib, one chunk. */
static inline uint32_t
r5_pm4_dma_ib(uint32_t* ib, uint64_t fenceGpu, uint64_t dstGpu)
{
	const uint32_t eop = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| kWlCacheFlushTs | (5u << 8);
	uint32_t n = 0;
	uint64_t startGpu = fenceGpu + kFenceTsStart;
	ib[n++] = r5_pm4_packet3(kWlReleaseMem, 6);
	ib[n++] = eop;
	ib[n++] = kWlDataSelCounter;
	ib[n++] = (uint32_t)startGpu;
	ib[n++] = (uint32_t)(startGpu >> 32);
	ib[n++] = 0;
	ib[n++] = 0;
	ib[n++] = 0;
	ib[n++] = r5_pm4_packet3(kWlDmaData, 5);
	ib[n++] = kWlDmaCpSync | kWlDmaSrcData;
	ib[n++] = kK2Pattern;
	ib[n++] = 0;
	ib[n++] = (uint32_t)dstGpu;
	ib[n++] = (uint32_t)(dstGpu >> 32);
	ib[n++] = kWlDmaRawWait | kDmaChunk;
	uint64_t endGpu = fenceGpu + kFenceTsEnd;
	ib[n++] = r5_pm4_packet3(kWlReleaseMem, 6);
	ib[n++] = eop;
	ib[n++] = kWlDataSelCounter;
	ib[n++] = (uint32_t)endGpu;
	ib[n++] = (uint32_t)(endGpu >> 32);
	ib[n++] = 0;
	ib[n++] = 0;
	ib[n++] = 0;
	return n;
}


/* The 12-dword submit used by the workload, the compute path and K2.
 * dataSel is 1 << 29. intSel is 0, or 2 << 24 on the user-job fence. */
static inline void
r5_pm4_submit(uint32_t* packets, uint64_t ibGpu, uint32_t ibDwords,
	uint64_t sequenceGpu, uint64_t sequence, uint32_t dataSel, uint32_t intSel)
{
	const uint32_t eop = (1u << 16) | (1u << 17) | (1u << 15) | (1u << 21)
		| kWlCacheFlushTs | (5u << 8);
	packets[0] = r5_pm4_packet3(kWlIndirectBuffer, 2);
	packets[1] = (uint32_t)ibGpu & ~3u;
	packets[2] = (uint32_t)((ibGpu >> 32) & 0xffffu);
	packets[3] = kWlIbValid | ibDwords;
	packets[4] = r5_pm4_packet3(kWlReleaseMem, 6);
	packets[5] = eop;
	packets[6] = dataSel | intSel;
	packets[7] = (uint32_t)sequenceGpu;
	packets[8] = (uint32_t)(sequenceGpu >> 32);
	packets[9] = (uint32_t)sequence;
	packets[10] = (uint32_t)(sequence >> 32);
	packets[11] = 0;
}


/* The SWITCH_BUFFER pair around the user body. */
static inline void
r5_pm4_switch_pair(uint32_t* packets)
{
	packets[0] = r5_pm4_packet3(0x8b, 0);
	packets[1] = 0;
	packets[2] = r5_pm4_packet3(0x8b, 0);
	packets[3] = 0;
}


/* gpu_stage_dma. Seven dwords. Destination select 0. */
static inline void
r5_pm4_stage_dma(uint32_t* words, uint64_t src, uint64_t dest)
{
	words[0] = 0xc0055000u;
	words[1] = kStageDmaCtrl;
	words[2] = (uint32_t)src;
	words[3] = (uint32_t)(src >> 32);
	words[4] = (uint32_t)dest;
	words[5] = (uint32_t)(dest >> 32);
	words[6] = 0x80000080u;
}

#endif
