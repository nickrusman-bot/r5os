/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The hang policy. One pure function: the state and one event go in,
 * the new state and the actions come out. No lock, no register, no log.
 * The kernel core runs the actions; scripts/test-r5-hang.cpp tests it.
 *
 * A hang is a GFX job past the watchdog (kWatchdogUs, the same 10 s as
 * amdgpu.lockup_timeout). The first hang of a boot resets the GPU
 * (mode 2), then the fixed GFX9 sequences and the self-test run again.
 * A second hang, a failed reset, or a failed self-test turns the GPU
 * off until the next boot. Every context that existed at a hang is
 * lost. docs/GPU-ARCHITECTURE.md "When things go wrong".
 */

#ifndef R5_HANG_H
#define R5_HANG_H

#include <stdint.h>

enum {
	R5_HANG_RUNNING = 0,
	R5_HANG_RESETTING = 1,
	R5_HANG_TESTING = 2,
	R5_HANG_OFF = 3
};

enum {
	R5_HANG_EV_HANG = 0,
	R5_HANG_EV_RESET_OK = 1,
	R5_HANG_EV_RESET_FAILED = 2,
	R5_HANG_EV_SELFTEST_OK = 3,
	R5_HANG_EV_SELFTEST_FAILED = 4
};

/* Actions, in the order the caller runs them. */
enum {
	R5_HANG_ACT_LOG = 1u << 0,		/* the gpu hang line */
	R5_HANG_ACT_LOSE = 1u << 1,		/* every context now is lost */
	R5_HANG_ACT_RESET = 1u << 2,	/* mode 2, then the fixed sequences */
	R5_HANG_ACT_SELFTEST = 1u << 3,
	R5_HANG_ACT_REOPEN = 1u << 4,	/* new contexts and submits again */
	R5_HANG_ACT_OFF = 1u << 5		/* off until reboot, line with reason */
};

enum {
	R5_HANG_WHY_NONE = 0,
	R5_HANG_WHY_SECOND_HANG = 1,
	R5_HANG_WHY_RESET_FAILED = 2,
	R5_HANG_WHY_SELFTEST_FAILED = 3
};

typedef struct r5_hang {
	uint32_t	state;
	uint32_t	hangs;
	uint32_t	why;
} r5_hang;


static inline uint32_t
r5_hang_off(r5_hang* out, uint32_t why)
{
	out->state = R5_HANG_OFF;
	out->why = why;
	return R5_HANG_ACT_OFF;
}


/* in and out may be the same struct. An event that does not fit the
 * state (a reset result while running, anything while off) returns 0
 * and leaves the state as it was. */
static inline uint32_t
r5_hang_step(const r5_hang* in, uint32_t event, r5_hang* out)
{
	r5_hang next = *in;
	uint32_t actions = 0;
	switch (next.state) {
		case R5_HANG_RUNNING:
			if (event != R5_HANG_EV_HANG)
				break;
			next.hangs++;
			actions = R5_HANG_ACT_LOG | R5_HANG_ACT_LOSE;
			if (next.hangs >= 2)
				actions |= r5_hang_off(&next, R5_HANG_WHY_SECOND_HANG);
			else {
				next.state = R5_HANG_RESETTING;
				actions |= R5_HANG_ACT_RESET;
			}
			break;
		case R5_HANG_RESETTING:
			if (event == R5_HANG_EV_RESET_OK) {
				next.state = R5_HANG_TESTING;
				actions = R5_HANG_ACT_SELFTEST;
			} else if (event == R5_HANG_EV_RESET_FAILED)
				actions = r5_hang_off(&next, R5_HANG_WHY_RESET_FAILED);
			break;
		case R5_HANG_TESTING:
			if (event == R5_HANG_EV_SELFTEST_OK) {
				next.state = R5_HANG_RUNNING;
				actions = R5_HANG_ACT_REOPEN;
			} else if (event == R5_HANG_EV_SELFTEST_FAILED)
				actions = r5_hang_off(&next, R5_HANG_WHY_SELFTEST_FAILED);
			break;
		default:
			break;
	}
	*out = next;
	return actions;
}


/* The words of the off line. */
static inline const char*
r5_hang_why(uint32_t why)
{
	if (why == R5_HANG_WHY_SECOND_HANG)
		return "second hang";
	if (why == R5_HANG_WHY_RESET_FAILED)
		return "reset failed";
	if (why == R5_HANG_WHY_SELFTEST_FAILED)
		return "selftest failed";
	return "none";
}


/* One ring, in order: a job is done once the fence reached its
 * sequence. A later job of another client may already have passed it,
 * so the test is "at or past", never "equal" (AFTER-M8 item 4). */
static inline int
r5_fence_reached(uint64_t seen, uint64_t sequence)
{
	return seen >= sequence;
}


/* A job hangs when its fence is still out at the watchdog limit. A job
 * whose fence already passed is not a hang, whatever its age. */
static inline int
r5_job_hung(uint64_t seen, uint64_t sequence, int64_t ageUs, int64_t limitUs)
{
	if (r5_fence_reached(seen, sequence))
		return 0;
	return ageUs >= limitUs;
}

#endif
