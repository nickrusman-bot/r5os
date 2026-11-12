/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The isolation command list. The program and scripts/check-pm4-emit.cpp
 * both include this, so the checker sees the words the program submits.
 */
#ifndef R5_ISOLATION_IB_H
#define R5_ISOLATION_IB_H

#include <stdint.h>


/* COPY_DATA reads dest. WRITE_DATA then writes dest. Both use the
 * caller's page tables, so a foreign dest faults that file.
 * Stick 31 hung in this COPY_DATA. (1 << 16) | (1 << 20) is
 * SRC_SEL 0 and DST_SEL 0, register to register.
 *
 * Mesa 25.3.6 src/amd/common/sid.h lines 129-144:
 *   COPY_DATA_SRC_SEL(x)   ((x) & 0xf)         SRC_MEM is 1
 *   COPY_DATA_DST_SEL(x)   ((x) & 0xf) << 8    DST_MEM is 5
 *   COPY_DATA_COUNT_SEL    (1 << 16)
 *   COPY_DATA_WR_CONFIRM   (1 << 20)
 * Memory to memory is SRC_SEL(1) | DST_SEL(5) | COUNT_SEL | WR_CONFIRM.
 */
static inline void
r5_isolation_fill_ib(uint32_t* words, uint64_t dest, uint64_t readBack)
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

#endif
