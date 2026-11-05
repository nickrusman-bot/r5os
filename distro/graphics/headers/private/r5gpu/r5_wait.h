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

#endif
