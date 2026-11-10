/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The eight-dword self-test indirect buffer. The program and
 * scripts/check-pm4-emit.cpp both include this.
 */
#ifndef R5_SELFTEST_IB_H
#define R5_SELFTEST_IB_H

#include <stdint.h>


static inline void
r5_selftest_fill_ib(uint32_t* packet, uint64_t word)
{
	uint32_t nop = (3u << 30) | (0x3fffu << 16) | (0x10u << 8);
	packet[0] = (3u << 30) | (3u << 16) | (0x37u << 8);
	packet[1] = (5u << 8) | (1u << 20);
	packet[2] = (uint32_t)word;
	packet[3] = (uint32_t)(word >> 32);
	packet[4] = 0xDEADBEEFu;
	packet[5] = nop;
	packet[6] = nop;
	packet[7] = nop;
}

#endif
