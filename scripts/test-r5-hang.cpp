/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host test for the hang policy (r5_hang.h) and the PM4 hang job
 * (r5_hang_ib.h). Run from the repository root:
 *
 *   g++ -std=c++17 -Wall -Wextra -Werror \
 *     -Idistro/graphics/headers/private/r5gpu \
 *     -Idistro/graphics/src/add-ons/accelerants/radeon_hd/common \
 *     scripts/test-r5-hang.cpp -o /tmp/test-r5-hang && /tmp/test-r5-hang
 *
 * Pass line: ok hang policy <n> cases. Fail line: fail hang <case>.
 */

#include "r5_hang.h"
#include "r5_hang_ib.h"
#include "r5_vmid.h"

#include <stdio.h>
#include <string.h>


static int sCases = 0;


static int
fail(const char* what)
{
	fprintf(stderr, "fail hang %s\n", what);
	return 1;
}


/* One event: the actions and the state after it must match. */
static int
step(r5_hang* s, uint32_t event, uint32_t wantActions, uint32_t wantState,
	const char* what)
{
	uint32_t got = r5_hang_step(s, event, s);
	if (got != wantActions || s->state != wantState) {
		fprintf(stderr, "fail hang %s actions %x want %x state %u want %u\n",
			what, got, wantActions, s->state, wantState);
		return 1;
	}
	return 0;
}


static const uint32_t kFirst = R5_HANG_ACT_LOG | R5_HANG_ACT_LOSE
	| R5_HANG_ACT_RESET;


static int
one_hang()
{
	r5_hang s;
	memset(&s, 0, sizeof(s));
	if (step(&s, R5_HANG_EV_HANG, kFirst, R5_HANG_RESETTING, "one hang")
		|| step(&s, R5_HANG_EV_RESET_OK, R5_HANG_ACT_SELFTEST,
			R5_HANG_TESTING, "one reset")
		|| step(&s, R5_HANG_EV_SELFTEST_OK, R5_HANG_ACT_REOPEN,
			R5_HANG_RUNNING, "one selftest"))
		return 1;
	if (s.hangs != 1 || s.why != R5_HANG_WHY_NONE)
		return fail("one count");
	sCases++;
	return 0;
}


static int
two_hangs()
{
	r5_hang s;
	memset(&s, 0, sizeof(s));
	if (step(&s, R5_HANG_EV_HANG, kFirst, R5_HANG_RESETTING, "two first")
		|| step(&s, R5_HANG_EV_RESET_OK, R5_HANG_ACT_SELFTEST,
			R5_HANG_TESTING, "two reset")
		|| step(&s, R5_HANG_EV_SELFTEST_OK, R5_HANG_ACT_REOPEN,
			R5_HANG_RUNNING, "two selftest")
		|| step(&s, R5_HANG_EV_HANG,
			R5_HANG_ACT_LOG | R5_HANG_ACT_LOSE | R5_HANG_ACT_OFF,
			R5_HANG_OFF, "two second"))
		return 1;
	if (s.why != R5_HANG_WHY_SECOND_HANG
		|| strcmp(r5_hang_why(s.why), "second hang") != 0)
		return fail("two why");
	/* Off stays off: a third hang and a stray result do nothing. */
	if (step(&s, R5_HANG_EV_HANG, 0, R5_HANG_OFF, "two third")
		|| step(&s, R5_HANG_EV_SELFTEST_OK, 0, R5_HANG_OFF, "two stray"))
		return 1;
	if (s.hangs != 2)
		return fail("two count");
	sCases++;
	return 0;
}


static int
reset_fail()
{
	r5_hang s;
	memset(&s, 0, sizeof(s));
	if (step(&s, R5_HANG_EV_HANG, kFirst, R5_HANG_RESETTING, "rf hang")
		|| step(&s, R5_HANG_EV_RESET_FAILED, R5_HANG_ACT_OFF, R5_HANG_OFF,
			"rf reset"))
		return 1;
	if (strcmp(r5_hang_why(s.why), "reset failed") != 0)
		return fail("rf why");
	sCases++;
	return 0;
}


static int
selftest_fail()
{
	r5_hang s;
	memset(&s, 0, sizeof(s));
	if (step(&s, R5_HANG_EV_HANG, kFirst, R5_HANG_RESETTING, "sf hang")
		|| step(&s, R5_HANG_EV_RESET_OK, R5_HANG_ACT_SELFTEST,
			R5_HANG_TESTING, "sf reset")
		|| step(&s, R5_HANG_EV_SELFTEST_FAILED, R5_HANG_ACT_OFF, R5_HANG_OFF,
			"sf selftest"))
		return 1;
	if (strcmp(r5_hang_why(s.why), "selftest failed") != 0)
		return fail("sf why");
	sCases++;
	return 0;
}


/* Results that do not fit the state change nothing. */
static int
stray_events()
{
	r5_hang s;
	memset(&s, 0, sizeof(s));
	if (step(&s, R5_HANG_EV_RESET_OK, 0, R5_HANG_RUNNING, "stray reset")
		|| step(&s, R5_HANG_EV_SELFTEST_FAILED, 0, R5_HANG_RUNNING,
			"stray selftest"))
		return 1;
	if (step(&s, R5_HANG_EV_HANG, kFirst, R5_HANG_RESETTING, "stray hang")
		|| step(&s, R5_HANG_EV_HANG, 0, R5_HANG_RESETTING, "hang in reset")
		|| step(&s, R5_HANG_EV_SELFTEST_OK, 0, R5_HANG_RESETTING,
			"selftest in reset"))
		return 1;
	sCases++;
	return 0;
}


/* AFTER-M8 item 4. The self-test job is sequence 5. A second client
 * submitted 6 to 9, and the fence already reads 9 when the self-test
 * looks. Its job is done, not hung, at any age. A job that really
 * stopped (fence 4) is a hang only at the limit. While the second
 * client works, a hang still loses every context, its own as well. */
static int
second_client()
{
	const int64_t limit = 10000000;
	if (!r5_fence_reached(9, 5) || !r5_fence_reached(5, 5)
		|| r5_fence_reached(4, 5))
		return fail("fence reached");
	if (r5_job_hung(9, 5, 2 * limit, limit))
		return fail("passed fence counted as hang");
	if (r5_job_hung(4, 5, limit - 1, limit))
		return fail("hang before the limit");
	if (!r5_job_hung(4, 5, limit, limit))
		return fail("hang at the limit");
	r5_hang s;
	memset(&s, 0, sizeof(s));
	uint32_t got = r5_hang_step(&s, R5_HANG_EV_HANG, &s);
	if ((got & R5_HANG_ACT_LOSE) == 0)
		return fail("second client keeps its context");
	sCases++;
	return 0;
}


/* SELFTEST-FINDINGS.md: between two polls of the self-test wait, the
 * fence jumps from sequence - 1 to sequence + 1 (a second client's job
 * retired right after the self-test's). The wait must end "ok" on the
 * second poll; the old "seen == sequence" test never matched and timed
 * out after 5 s. */
static int
fence_jump()
{
	const uint64_t sequence = 314624;
	const uint64_t polls[] = { sequence - 1, sequence + 1, sequence + 1,
		sequence + 2 };
	int landed = -1;
	int oldLanded = -1;
	for (int i = 0; i < 4; i++) {
		if (landed < 0 && r5_fence_reached(polls[i], sequence))
			landed = i;
		if (oldLanded < 0 && polls[i] == sequence)
			oldLanded = i;
	}
	if (landed != 1)
		return fail("fence jump not ok on the second poll");
	if (oldLanded != -1)
		return fail("fence jump case does not reproduce the old timeout");
	sCases++;
	return 0;
}


/* Review of PR 4222, item 4. File A (owner 7) had VMID 2 with job 10
 * when the chip hung; it is lost and keeps its tree. After the reset
 * the fence reads 10. File B (owner 8) opens and gets a VMID with a
 * flush. When A later frees its tree, B keeps its slot, and A's slot
 * never goes to two owners. */
static int
vmid_after_reset()
{
	r5_vmid_mgr m;
	r5_vmid_init_files(&m);
	int flush = 0;
	uint32_t a = r5_vmid_reserve(&m, 7, 0, 1, &flush);
	r5_vmid_arm(&m, 10);
	if (a != 2)
		return fail("vmid a");
	r5_vmid_after_reset(&m);
	flush = 0;
	uint32_t b = r5_vmid_reserve(&m, 8, 10, 1, &flush);
	r5_vmid_arm(&m, 11);
	if (b == 0 || b < 2 || flush != 1)
		return fail("vmid b after reset");
	r5_vmid_release(&m, 7, 10);
	if (m.slot[b].owner != 8)
		return fail("vmid a release cleared b");
	for (uint32_t i = 2; i <= 15; i++) {
		if (i != b && m.slot[i].owner == 8)
			return fail("vmid b twice");
	}
	/* B again, same owner: its slot, flushed once already. */
	flush = 0;
	if (r5_vmid_reserve(&m, 8, 10, 1, &flush) != b || flush != 0)
		return fail("vmid b again");
	r5_vmid_cancel(&m);
	/* A held reservation at the hang is dropped by the reset. */
	r5_vmid_reserve(&m, 9, 11, 1, &flush);
	r5_vmid_after_reset(&m);
	if (m.hold != 0 || m.chosen != 0)
		return fail("vmid hold after reset");
	sCases++;
	return 0;
}


/* The hang job: one WAIT_REG_MEM, memory space, equal, on a dword that
 * stays 0 and is compared with 1. scripts/check-pm4.py checks the bits
 * against Mesa sid.h; this checks the layout and the refusals. */
static int
hang_ib()
{
	uint32_t words[R5_HANG_IB_DWORDS + 1];
	const uint64_t va = 0x0000800000123440ull;
	memset(words, 0xff, sizeof(words));
	if (r5_hang_ib(words, R5_HANG_IB_DWORDS, va) != R5_HANG_IB_DWORDS)
		return fail("ib size");
	if (words[0] != 0xc0053c00u || words[1] != 0x00000013u
		|| words[2] != 0x00123440u || words[3] != 0x00008000u
		|| words[4] != 1u || words[5] != 0xffffffffu || words[6] == 0)
		return fail("ib words");
	if (words[R5_HANG_IB_DWORDS] != 0xffffffffu)
		return fail("ib overrun");
	if (r5_hang_ib(words, R5_HANG_IB_DWORDS - 1, va) != 0)
		return fail("ib short buffer");
	if (r5_hang_ib(words, R5_HANG_IB_DWORDS, va | 2u) != 0)
		return fail("ib unaligned");
	if (r5_hang_ib(NULL, R5_HANG_IB_DWORDS, va) != 0)
		return fail("ib null");
	sCases++;
	return 0;
}


int
main()
{
	if (one_hang() || two_hangs() || reset_fail() || selftest_fail()
		|| stray_events() || second_client() || fence_jump() || hang_ib()
		|| vmid_after_reset())
		return 1;
	printf("ok hang policy %d cases\n", sCases);
	return 0;
}
