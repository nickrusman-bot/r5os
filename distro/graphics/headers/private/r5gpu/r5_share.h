/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M11 step 3b: sharing a GPU buffer with app_server (docs/APP-SERVER-
 * COMPOSITOR.md, "Zero-copy design"). The export number, the rights
 * check, the expiry, the slot limits and the reference count. Pure
 * functions, so the host test (scripts/test-r5-share.cpp) covers every
 * case. The kernel (r5_gpu_core.cpp) calls them under the GPU lock.
 */

#ifndef R5_SHARE_H
#define R5_SHARE_H

#include <stdint.h>
#include <string.h>

/* Starting values (decision 15b). The stick measures the real ones. */
#define R5_SHARE_EXPORTS_PER_FILE 32u
#define R5_SHARE_LIFE_US 5000000ll
#define R5_SHARE_MAX_BO 1024u
#define R5_SHARE_MAX_FILE_BO 512u
#define R5_SHARE_RESERVE 64u

#define R5_SHARE_BYTES 16u

/* One pending export. backing is an index into the kernel's backing
 * table; the record holds one reference on it. */
typedef struct {
	int			live;
	uint8_t		token[R5_SHARE_BYTES];
	uint32_t	backing;
	int32_t		team;
	int64_t		made;
	uint32_t	target;
} r5_share_export;

enum {
	R5_SHARE_OK = 0,
	R5_SHARE_CALLER = 1,	/* not app_server */
	R5_SHARE_TEAM = 2,		/* not the team pinned at export */
	R5_SHARE_TOKEN = 3,		/* no such number, or already imported */
	R5_SHARE_EXPIRED = 4,	/* older than R5_SHARE_LIFE_US */
	R5_SHARE_LOST = 5,		/* the exporting file lost its context */
	R5_SHARE_LIMIT = 6,		/* R5_SHARE_EXPORTS_PER_FILE pending */
	R5_SHARE_TARGET = 7,	/* a target, team or flag version 1 refuses */
	R5_SHARE_FILE = 8,		/* this file holds R5_SHARE_MAX_FILE_BO */
	R5_SHARE_SYSTEM = 9,	/* every slot is taken */
	R5_SHARE_RESERVE_LEFT = 10	/* only app_server's reserve is left */
};


/* The word that the refusal line carries (BO_IMPORT <why>). */
static inline const char*
r5_share_reason(int reason)
{
	switch (reason) {
		case R5_SHARE_OK: return "ok";
		case R5_SHARE_CALLER: return "caller";
		case R5_SHARE_TEAM: return "team";
		case R5_SHARE_TOKEN: return "token";
		case R5_SHARE_EXPIRED: return "expired";
		case R5_SHARE_LOST: return "lost";
		case R5_SHARE_LIMIT: return "limit";
		case R5_SHARE_TARGET: return "target";
		case R5_SHARE_FILE: return "handles";
		case R5_SHARE_SYSTEM: return "slots";
		case R5_SHARE_RESERVE_LEFT: return "reserve";
	}
	return "unknown";
}


/* The export number. The low half is the counter times an odd constant:
 * a bijection, so two counters never give the same number, and numbers
 * that follow each other are not consecutive. The high half is two
 * values of secure_random_value(). The counter starts at 1 and only goes
 * up, so no number is ever given out twice. Not a secret: the rights
 * check below is the boundary. */
static inline void
r5_share_make_token(uint64_t counter, uint32_t random0, uint32_t random1,
	uint8_t token[R5_SHARE_BYTES])
{
	uint64_t low = counter * 0x9e3779b97f4a7c15ull;
	uint64_t high = ((uint64_t)random0 << 32) | (uint64_t)random1;
	for (uint32_t i = 0; i < 8; i++) {
		token[i] = (uint8_t)(low >> (i * 8));
		token[8 + i] = (uint8_t)(high >> (i * 8));
	}
}


/* Younger than five seconds is still valid. */
static inline int
r5_share_expired(int64_t now, int64_t made)
{
	return now - made >= R5_SHARE_LIFE_US;
}


/* The live record with this number, or -1. */
static inline int
r5_share_find(const r5_share_export* table, uint32_t count,
	const uint8_t token[R5_SHARE_BYTES])
{
	for (uint32_t i = 0; i < count; i++) {
		if (table[i].live
			&& memcmp(table[i].token, token, R5_SHARE_BYTES) == 0)
			return (int)i;
	}
	return -1;
}


/* The number of live records of one file. */
static inline uint32_t
r5_share_pending(const r5_share_export* table, uint32_t count)
{
	uint32_t pending = 0;
	for (uint32_t i = 0; i < count; i++) {
		if (table[i].live)
			pending++;
	}
	return pending;
}


/* BO_EXPORT. Version 1 knows only the compositor as target. */
static inline int
r5_share_check_export(uint32_t target, uint32_t compositor, int32_t targetTeam,
	uint32_t flags, uint32_t pending)
{
	if (target != compositor || targetTeam != 0 || flags != 0)
		return R5_SHARE_TARGET;
	if (pending >= R5_SHARE_EXPORTS_PER_FILE)
		return R5_SHARE_LIMIT;
	return R5_SHARE_OK;
}


/* BO_IMPORT. record is the live record with the number, or NULL.
 * clientTeam is ServerApp::ClientTeam() of the connection the number
 * came on, passed by app_server. */
static inline int
r5_share_check_import(int callerIsAppServer, uint32_t flags,
	const r5_share_export* record, int32_t clientTeam, int64_t now,
	int exporterLost)
{
	if (!callerIsAppServer)
		return R5_SHARE_CALLER;
	if (flags != 0)
		return R5_SHARE_TARGET;
	if (record == NULL || !record->live)
		return R5_SHARE_TOKEN;
	if (r5_share_expired(now, record->made))
		return R5_SHARE_EXPIRED;
	if (record->team != clientTeam)
		return R5_SHARE_TEAM;
	if (exporterLost)
		return R5_SHARE_LOST;
	return R5_SHARE_OK;
}


/* One more buffer slot. live is the number of live slots in the system,
 * handles the number this file holds. The last R5_SHARE_RESERVE slots
 * are only for app_server. */
static inline int
r5_share_slot_check(uint32_t live, uint32_t handles, int callerIsAppServer)
{
	if (handles >= R5_SHARE_MAX_FILE_BO)
		return R5_SHARE_FILE;
	if (live >= R5_SHARE_MAX_BO)
		return R5_SHARE_SYSTEM;
	if (!callerIsAppServer && live >= R5_SHARE_MAX_BO - R5_SHARE_RESERVE)
		return R5_SHARE_RESERVE_LEFT;
	return R5_SHARE_OK;
}


/* The <n> of "radeon_hd: buffer limit <n> reached, team <id>". */
static inline uint32_t
r5_share_limit_of(int reason)
{
	if (reason == R5_SHARE_FILE)
		return R5_SHARE_MAX_FILE_BO;
	if (reason == R5_SHARE_RESERVE_LEFT)
		return R5_SHARE_MAX_BO - R5_SHARE_RESERVE;
	return R5_SHARE_MAX_BO;
}


/* The reference count of a backing: each live slot (a parked one too)
 * and each pending export. Drop returns 1 for the last reference, the
 * one that frees the memory. A count of 0 is never dropped again. */
static inline void
r5_share_ref_hold(uint32_t* refs)
{
	(*refs)++;
}


static inline int
r5_share_ref_drop(uint32_t* refs)
{
	if (*refs == 0)
		return 0;
	(*refs)--;
	return *refs == 0;
}


/* app_server believes the kernel: the size from BO_IMPORT against the
 * width, height and pitch from the program's message. Four bytes per
 * pixel (the formats the copy path accepts). */
static inline int
r5_share_size_ok(uint32_t width, uint32_t height, uint32_t pitch,
	uint64_t bytes)
{
	if (width == 0 || height == 0 || pitch == 0)
		return 0;
	if ((uint64_t)pitch < (uint64_t)width * 4ull)
		return 0;
	return (uint64_t)pitch * (uint64_t)height <= bytes;
}

#endif
