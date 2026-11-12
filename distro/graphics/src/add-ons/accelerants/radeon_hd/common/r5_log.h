/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Which radeon_hd lines are printed. Errors are always on, and at most
 * 32 events per boot. A dump is one event and then at most 64 lines.
 * Debug lines need gpu_debug. Test lines need gpu_selftest. Each
 * once-line has its own id and is printed on the first call only.
 */

#ifndef R5_LOG_H
#define R5_LOG_H

enum {
	R5_LOG_ERROR = 1,
	R5_LOG_DEBUG = 2,
	R5_LOG_TEST = 3,
	R5_LOG_ONCE = 4,
	/* onceId < 0 starts a dump and counts one error event. A later
	 * call with onceId >= 0 prints one dump line, up to 64. */
	R5_LOG_DUMP = 5
};

enum {
	R5_ONCE_RB = 0
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
	if (kind == R5_LOG_DUMP) {
		static int dumpLines;
		static int dumpOpen;
		if (onceId < 0) {
			dumpOpen = 0;
			dumpLines = 0;
			if (*errors >= 32)
				return 0;
			*errors += 1;
			dumpOpen = 1;
			return 1;
		}
		if (dumpOpen == 0 || dumpLines >= 64)
			return 0;
		dumpLines += 1;
		return 1;
	}
	return 0;
}

#endif
