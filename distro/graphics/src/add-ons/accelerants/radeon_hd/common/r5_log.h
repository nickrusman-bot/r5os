/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Which radeon_hd lines are printed. Errors are always on, and at most
 * 32 of them per boot. Debug lines need gpu_debug. Test lines need
 * gpu_selftest. A once-line is printed on the first call only.
 */

#ifndef R5_LOG_H
#define R5_LOG_H

enum {
	R5_LOG_ERROR = 1,
	R5_LOG_DEBUG = 2,
	R5_LOG_TEST = 3,
	R5_LOG_ONCE = 4
};

/* errors and onceBits are updated. Returns 1 when the line is printed. */
static inline int
r5_log_allow(int debugOn, int selftestOn, int kind, int* errors, int* onceBits,
	int onceId)
{
	if (errors == 0 || onceBits == 0)
		return 0;
	if (kind == R5_LOG_ERROR) {
		if (*errors >= 32)
			return 0;
		*errors += 1;
		return 1;
	}
	if (kind == R5_LOG_DEBUG)
		return debugOn ? 1 : 0;
	if (kind == R5_LOG_TEST)
		return selftestOn ? 1 : 0;
	if (kind == R5_LOG_ONCE) {
		int bit = 1 << (onceId & 31);
		if ((*onceBits & bit) != 0)
			return 0;
		*onceBits |= bit;
		return 1;
	}
	return 0;
}

#endif
