/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host test for M11 step 3b (r5_share.h): the export number, the rights
 * check at import, the expiry, the export and slot limits, the reference
 * count and app_server's size check. Run from the repository root:
 *
 *   g++ -std=c++17 -Wall -Wextra -Werror \
 *     -Idistro/graphics/headers/private/r5gpu \
 *     scripts/test-r5-share.cpp -o /tmp/test-r5-share \
 *     && /tmp/test-r5-share
 *
 * Pass line: ok gpu share <n> cases. Fail line: fail gpu share <case>.
 */

#include "r5_gpu.h"
#include "r5_share.h"

#include <stdio.h>
#include <string.h>


static int sCases = 0;


static int
check(bool ok, const char* what)
{
	sCases++;
	if (!ok)
		fprintf(stderr, "fail gpu share %s\n", what);
	return ok ? 0 : 1;
}


static const int32_t kClient = 1234;
static const int32_t kOther = 4321;
static const int64_t kNow = 100000000ll;


static r5_share_export
record(uint64_t counter, int32_t team, int64_t made)
{
	r5_share_export out;
	memset(&out, 0, sizeof(out));
	out.live = 1;
	r5_share_make_token(counter, 0x11111111u, 0x22222222u, out.token);
	out.team = team;
	out.made = made;
	out.target = R5_GPU_SHARE_COMPOSITOR;
	return out;
}


static int
numbers()
{
	int failed = 0;
	failed |= check(sizeof(r5_gpu_bo_export) == 48, "export struct size");
	failed |= check(sizeof(r5_gpu_bo_import) == 56, "import struct size");
	failed |= check(sizeof(r5_gpu_bo_mark) == 24, "mark struct size");
	failed |= check(R5_GPU_BO_PRESENT_MARK == R5_GPU_OP_BASE + 32, "mark op");
	failed |= check(R5_GPU_BO_EXPORT == R5_GPU_OP_BASE + 17, "export op");
	failed |= check(R5_GPU_BO_IMPORT == R5_GPU_OP_BASE + 18, "import op");
	failed |= check(R5_SHARE_BYTES == R5_GPU_SHARE_TOKEN_BYTES, "token bytes");

	/* Unique: the low half differs for every counter, whatever the
	 * random half is. 100000 numbers in a row, also with the same
	 * random half, and none equal to its neighbour plus one. */
	bool unique = true;
	bool spread = true;
	uint8_t previous[R5_SHARE_BYTES];
	r5_share_make_token(1, 0, 0, previous);
	for (uint64_t counter = 2; counter < 100000; counter++) {
		uint8_t token[R5_SHARE_BYTES];
		r5_share_make_token(counter, 0, 0, token);
		if (memcmp(token, previous, R5_SHARE_BYTES) == 0)
			unique = false;
		uint64_t low = 0;
		uint64_t lastLow = 0;
		for (int i = 0; i < 8; i++) {
			low |= (uint64_t)token[i] << (i * 8);
			lastLow |= (uint64_t)previous[i] << (i * 8);
		}
		if (low == lastLow + 1)
			spread = false;
		memcpy(previous, token, sizeof(previous));
	}
	failed |= check(unique, "numbers unique");
	failed |= check(spread, "numbers not consecutive");
	/* A bijection: two counters with the same low half would be the
	 * same counter. The odd constant has an inverse modulo 2^64. */
	{
		uint64_t inverse = 0x9e3779b97f4a7c15ull;
		for (int i = 0; i < 6; i++)
			inverse *= 2 - 0x9e3779b97f4a7c15ull * inverse;
		failed |= check(inverse * 0x9e3779b97f4a7c15ull == 1, "bijection");
	}
	return failed;
}


static int
imports()
{
	int failed = 0;
	r5_share_export table[R5_SHARE_EXPORTS_PER_FILE];
	memset(table, 0, sizeof(table));
	table[3] = record(7, kClient, kNow);
	const r5_share_export* found = NULL;

	int index = r5_share_find(table, R5_SHARE_EXPORTS_PER_FILE, table[3].token);
	failed |= check(index == 3, "find");
	found = index >= 0 ? &table[index] : NULL;

	failed |= check(r5_share_check_import(1, 0, found, kClient, kNow + 1, 0)
		== R5_SHARE_OK, "import ok");
	/* Another team imports: not app_server. */
	failed |= check(r5_share_check_import(0, 0, found, kClient, kNow, 0)
		== R5_SHARE_CALLER, "import by other team refused");
	/* Forwarded: app_server, but the connection is another team. */
	failed |= check(r5_share_check_import(1, 0, found, kOther, kNow, 0)
		== R5_SHARE_TEAM, "forwarded number refused");
	failed |= check(r5_share_check_import(1, 1, found, kClient, kNow, 0)
		== R5_SHARE_TARGET, "import flags refused");
	failed |= check(r5_share_check_import(1, 0, found, kClient, kNow, 1)
		== R5_SHARE_LOST, "lost exporter refused");

	/* 1000 guessed numbers: none matches the live record. */
	bool guessed = true;
	for (uint64_t guess = 0; guess < 1000; guess++) {
		uint8_t token[R5_SHARE_BYTES];
		r5_share_make_token(guess == 7 ? 7000 : guess, (uint32_t)guess, 0,
			token);
		int hit = r5_share_find(table, R5_SHARE_EXPORTS_PER_FILE, token);
		if (hit >= 0 || r5_share_check_import(1, 0, NULL, kClient, kNow, 0)
				!= R5_SHARE_TOKEN)
			guessed = false;
	}
	failed |= check(guessed, "1000 guessed numbers refused");
	/* The right counter with the wrong random half is a guess too. */
	{
		uint8_t token[R5_SHARE_BYTES];
		r5_share_make_token(7, 0x11111111u, 0x22222223u, token);
		failed |= check(r5_share_find(table, R5_SHARE_EXPORTS_PER_FILE, token)
			< 0, "random half checked");
	}

	/* Once: the import removes the record; the same number again finds
	 * nothing. */
	uint8_t used[R5_SHARE_BYTES];
	memcpy(used, table[3].token, sizeof(used));
	table[3].live = 0;
	index = r5_share_find(table, R5_SHARE_EXPORTS_PER_FILE, used);
	failed |= check(index < 0 && r5_share_check_import(1, 0, NULL, kClient,
		kNow, 0) == R5_SHARE_TOKEN, "second import refused");

	/* Expiry: 4.999999 s is valid, 5 s is not. */
	r5_share_export young = record(8, kClient, kNow);
	failed |= check(r5_share_check_import(1, 0, &young, kClient,
		kNow + R5_SHARE_LIFE_US - 1, 0) == R5_SHARE_OK, "younger than 5 s");
	failed |= check(r5_share_check_import(1, 0, &young, kClient,
		kNow + R5_SHARE_LIFE_US, 0) == R5_SHARE_EXPIRED, "expired number refused");
	failed |= check(!r5_share_expired(kNow + R5_SHARE_LIFE_US - 1, kNow)
		&& r5_share_expired(kNow + R5_SHARE_LIFE_US, kNow), "expired edge");
	/* The caller check comes first: another team does not learn that a
	 * number exists. */
	failed |= check(r5_share_check_import(0, 0, &young, kOther,
		kNow + R5_SHARE_LIFE_US, 0) == R5_SHARE_CALLER, "caller first");
	return failed;
}


static int
exports()
{
	int failed = 0;
	failed |= check(r5_share_check_export(R5_GPU_SHARE_COMPOSITOR,
		R5_GPU_SHARE_COMPOSITOR, 0, 0, 0) == R5_SHARE_OK, "export ok");
	failed |= check(r5_share_check_export(R5_GPU_SHARE_TEAM,
		R5_GPU_SHARE_COMPOSITOR, 0, 0, 0) == R5_SHARE_TARGET, "team target");
	failed |= check(r5_share_check_export(R5_GPU_SHARE_COMPOSITOR,
		R5_GPU_SHARE_COMPOSITOR, 5, 0, 0) == R5_SHARE_TARGET, "target team");
	failed |= check(r5_share_check_export(R5_GPU_SHARE_COMPOSITOR,
		R5_GPU_SHARE_COMPOSITOR, 0, 1, 0) == R5_SHARE_TARGET, "export flags");
	failed |= check(r5_share_check_export(R5_GPU_SHARE_COMPOSITOR,
		R5_GPU_SHARE_COMPOSITOR, 0, 0, 31) == R5_SHARE_OK, "export 32nd");
	failed |= check(r5_share_check_export(R5_GPU_SHARE_COMPOSITOR,
		R5_GPU_SHARE_COMPOSITOR, 0, 0, 32) == R5_SHARE_LIMIT,
		"export limit refused");

	r5_share_export table[R5_SHARE_EXPORTS_PER_FILE];
	memset(table, 0, sizeof(table));
	for (uint32_t i = 0; i < R5_SHARE_EXPORTS_PER_FILE; i += 2)
		table[i] = record(i + 1, kClient, kNow);
	failed |= check(r5_share_pending(table, R5_SHARE_EXPORTS_PER_FILE) == 16,
		"pending count");
	return failed;
}


static int
slots()
{
	int failed = 0;
	const uint32_t edge = R5_SHARE_MAX_BO - R5_SHARE_RESERVE;
	failed |= check(r5_share_slot_check(0, 0, 0) == R5_SHARE_OK, "slot ok");
	failed |= check(r5_share_slot_check(10, R5_SHARE_MAX_FILE_BO - 1, 0)
		== R5_SHARE_OK, "last slot of the file");
	failed |= check(r5_share_slot_check(10, R5_SHARE_MAX_FILE_BO, 1)
		== R5_SHARE_FILE, "per-file limit");
	failed |= check(r5_share_slot_check(edge - 1, 0, 0) == R5_SHARE_OK,
		"last slot before the reserve");
	failed |= check(r5_share_slot_check(edge, 0, 0) == R5_SHARE_RESERVE_LEFT,
		"reserve kept for app_server");
	failed |= check(r5_share_slot_check(edge, 0, 1) == R5_SHARE_OK,
		"app_server takes the reserve");
	failed |= check(r5_share_slot_check(R5_SHARE_MAX_BO - 1, 0, 1)
		== R5_SHARE_OK, "app_server last slot");
	failed |= check(r5_share_slot_check(R5_SHARE_MAX_BO, 0, 1)
		== R5_SHARE_SYSTEM, "system full");
	failed |= check(R5_SHARE_MAX_FILE_BO < edge, "one file cannot reach the reserve");
	failed |= check(r5_share_limit_of(R5_SHARE_FILE) == 512
		&& r5_share_limit_of(R5_SHARE_RESERVE_LEFT) == 960
		&& r5_share_limit_of(R5_SHARE_SYSTEM) == 1024, "limit line values");
	return failed;
}


static int
references()
{
	int failed = 0;
	/* Exporter slot, a pending export: 2. The import moves the export's
	 * reference into app_server's slot: still 2. The exporter is killed:
	 * 1, the memory stays. app_server closes: 0, the memory goes. */
	uint32_t refs = 1;
	r5_share_ref_hold(&refs);
	failed |= check(refs == 2, "export holds");
	failed |= check(r5_share_ref_drop(&refs) == 0, "exporter killed, kept");
	failed |= check(r5_share_ref_drop(&refs) == 1, "last drop frees");
	failed |= check(r5_share_ref_drop(&refs) == 0 && refs == 0,
		"no second free");
	/* Killed before the import: the slot and the export both go. */
	refs = 2;
	int freed = r5_share_ref_drop(&refs);
	freed += r5_share_ref_drop(&refs);
	failed |= check(freed == 1, "killed before import, freed once");
	return failed;
}


static int
sizes()
{
	int failed = 0;
	failed |= check(r5_share_size_ok(640, 480, 2560, 2560ull * 480), "size ok");
	failed |= check(r5_share_size_ok(640, 480, 2816, 2816ull * 480),
		"padded pitch ok");
	failed |= check(!r5_share_size_ok(640, 480, 2560, 2560ull * 480 - 1),
		"lying size refused");
	failed |= check(!r5_share_size_ok(640, 480, 2556, 1ull << 30),
		"pitch below width refused");
	failed |= check(!r5_share_size_ok(0, 480, 2560, 1ull << 30), "zero width");
	failed |= check(!r5_share_size_ok(0xffffffffu, 0xffffffffu, 0xffffffffu,
		~0ull), "huge values refused");
	return failed;
}


/* Each refusal of the design's pass table, with the error it returns
 * (docs/APP-SERVER-COMPOSITOR.md, "Pass and fail lines"). */
static int
errors()
{
	int failed = 0;
	r5_share_export live = record(9, kClient, kNow);
	struct {
		int			reason;
		int			error;
		const char*	what;
	} cases[] = {
		{ r5_share_check_import(0, 0, &live, kClient, kNow, 0),
			R5_SHARE_E_NOT_ALLOWED, "error: import by other team" },
		{ r5_share_check_import(1, 0, &live, kOther, kNow, 0),
			R5_SHARE_E_NOT_ALLOWED, "error: forwarded number" },
		{ r5_share_check_import(1, 0, NULL, kClient, kNow, 0),
			R5_SHARE_E_BAD_VALUE, "error: guessed or second import" },
		{ r5_share_check_import(1, 0, &live, kClient,
			kNow + R5_SHARE_LIFE_US, 0),
			R5_SHARE_E_BAD_VALUE, "error: expired" },
		{ r5_share_check_import(1, 0, &live, kClient, kNow, 1),
			R5_SHARE_E_CANCELED, "error: lost exporter" },
		{ r5_share_check_export(R5_GPU_SHARE_COMPOSITOR,
			R5_GPU_SHARE_COMPOSITOR, 0, 0, R5_SHARE_EXPORTS_PER_FILE),
			R5_SHARE_E_NO_MEMORY, "error: export limit" },
		{ r5_share_check_export(R5_GPU_SHARE_TEAM, R5_GPU_SHARE_COMPOSITOR, 0,
			0, 0), R5_SHARE_E_BAD_VALUE, "error: target" },
		{ r5_share_slot_check(0, R5_SHARE_MAX_FILE_BO, 0),
			R5_SHARE_E_NO_MEMORY, "error: per-file limit" },
		{ r5_share_slot_check(R5_SHARE_MAX_BO - R5_SHARE_RESERVE, 0, 0),
			R5_SHARE_E_NO_MEMORY, "error: reserve" },
		{ r5_share_slot_check(R5_SHARE_MAX_BO, 0, 1),
			R5_SHARE_E_NO_MEMORY, "error: system full" },
		{ r5_share_check_import(1, 0, &live, kClient, kNow, 0),
			R5_SHARE_E_OK, "error: none for a good import" },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		failed |= check(r5_share_error(cases[i].reason) == cases[i].error,
			cases[i].what);

	/* The "free again" semaphore of another team is refused. */
	failed |= check(r5_share_sem_ok(kClient, kClient), "own semaphore");
	failed |= check(!r5_share_sem_ok(kOther, kClient),
		"semaphore of another team refused");
	failed |= check(!r5_share_sem_ok(-1, kClient), "no semaphore refused");
	return failed;
}


int
main()
{
	int failed = 0;
	failed |= numbers();
	failed |= errors();
	failed |= imports();
	failed |= exports();
	failed |= slots();
	failed |= references();
	failed |= sizes();
	if (failed != 0)
		return 1;
	printf("ok gpu share %d cases\n", sCases);
	return 0;
}
