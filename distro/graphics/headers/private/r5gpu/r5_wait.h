/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * One decision for every wait. A timeout of zero does not mean the device
 * is lost. An expired timeout does.
 */

#ifndef R5_WAIT_H
#define R5_WAIT_H

#include <stdint.h>

enum {
	R5_WAIT_DONE = 0,
	R5_WAIT_AGAIN = 1,
	R5_WAIT_TIMEOUT = 2,
	R5_WAIT_POLL = 3
};

static inline int
r5_wait_step(uint64_t seen, uint64_t target, uint64_t timeoutUs, int expired)
{
	if (seen >= target)
		return R5_WAIT_DONE;
	if (timeoutUs == 0)
		return R5_WAIT_POLL;
	if (expired)
		return R5_WAIT_TIMEOUT;
	return R5_WAIT_AGAIN;
}


enum {
	R5_PACE_OK = 0,
	R5_PACE_LATE = 1,
	R5_PACE_TIMEOUT = 2,
	R5_PACE_WAIT = 3
};


/* A fence before 1s is on time. From 1s until 5s the step is late and
 * still continues. At 5s without that fence the step times out. */
static inline int
r5_pace(uint64_t elapsedUs, int landed)
{
	if (landed) {
		if (elapsedUs >= 1000000ull)
			return R5_PACE_LATE;
		return R5_PACE_OK;
	}
	if (elapsedUs >= 5000000ull)
		return R5_PACE_TIMEOUT;
	return R5_PACE_WAIT;
}

#endif
