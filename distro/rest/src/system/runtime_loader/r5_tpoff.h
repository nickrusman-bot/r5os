/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_TPOFF_H
#define R5_TPOFF_H

#include <stdint.h>

/* Offset from the thread pointer for an initial-exec TLS symbol.
 * templateBase is where this image's TLS template bytes are placed,
 * relative to the thread pointer. symbolOffset is the symbol's offset
 * in that template. addend is the relocation addend.
 * A load from the thread pointer uses this offset.
 */
static inline uint64_t
r5_tpoff_offset(uint64_t templateBase, uint64_t symbolOffset, int64_t addend)
{
	return templateBase + symbolOffset + (uint64_t)addend;
}

#endif
