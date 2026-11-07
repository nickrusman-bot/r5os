/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VMIDs 1-15, one owner at a time. Linux v6.12 amdgpu_ids.c
 * amdgpu_vmid_grab keeps a VMID while that address space has a job
 * in flight, and flushes when the VMID is given to another owner.
 * VMID 0 stays the kernel ring and is not in this table.
 */

#ifndef R5_VMID_H
#define R5_VMID_H

#include <stdint.h>
#include <string.h>

struct r5_vmid_slot {
	uint32_t	owner;
	uint64_t	fence;
};

struct r5_vmid_mgr {
	struct r5_vmid_slot	slot[16];
	uint32_t			lru[15];
	uint32_t			chosen;
	uint32_t			hold;
	uint32_t			hold_owner_prev;
	uint64_t			hold_fence_prev;
};


static inline void
r5_vmid_init(struct r5_vmid_mgr* m)
{
	uint32_t i;
	memset(m, 0, sizeof(*m));
	for (i = 0; i < 15u; i++)
		m->lru[i] = i + 1u;
}


static inline void
r5_vmid_touch(struct r5_vmid_mgr* m, uint32_t vmid)
{
	uint32_t tmp[15];
	uint32_t n = 0;
	uint32_t i;
	for (i = 0; i < 15u; i++) {
		if (m->lru[i] != vmid)
			tmp[n++] = m->lru[i];
	}
	tmp[n++] = vmid;
	for (i = 0; i < 15u; i++)
		m->lru[i] = tmp[i];
}


/* 1-15, or 0 when none is idle. *flush is 1 when the owner changes.
 * A reserved slot uses fence ~0 until r5_vmid_arm. */
static inline uint32_t
r5_vmid_reserve(struct r5_vmid_mgr* m, uint32_t owner, uint64_t retired,
	int* flush)
{
	uint32_t i;
	if (m == 0 || flush == 0 || owner == 0 || m->hold != 0)
		return 0;
	for (i = 1; i <= 15u; i++) {
		if (m->slot[i].owner != owner)
			continue;
		if (m->slot[i].fence == ~0ull || m->slot[i].fence <= retired)
			continue;
		*flush = 0;
		m->chosen = i;
		r5_vmid_touch(m, i);
		return i;
	}
	for (i = 0; i < 15u; i++) {
		uint32_t v = m->lru[i];
		if (m->slot[v].fence == ~0ull)
			continue;
		if (m->slot[v].fence > retired)
			continue;
		*flush = m->slot[v].owner != owner ? 1 : 0;
		m->hold = v;
		m->hold_owner_prev = m->slot[v].owner;
		m->hold_fence_prev = m->slot[v].fence;
		m->slot[v].owner = owner;
		m->slot[v].fence = ~0ull;
		m->chosen = v;
		r5_vmid_touch(m, v);
		return v;
	}
	return 0;
}


static inline void
r5_vmid_arm(struct r5_vmid_mgr* m, uint64_t job)
{
	if (m == 0 || m->chosen == 0 || m->chosen > 15u)
		return;
	m->slot[m->chosen].fence = job;
	m->hold = 0;
	m->chosen = 0;
}


static inline void
r5_vmid_cancel(struct r5_vmid_mgr* m)
{
	if (m == 0)
		return;
	if (m->hold != 0 && m->hold <= 15u) {
		m->slot[m->hold].owner = m->hold_owner_prev;
		m->slot[m->hold].fence = m->hold_fence_prev;
	}
	m->hold = 0;
	m->chosen = 0;
}


/* Drops an owner whose last job has retired. A held reservation stays. */
static inline void
r5_vmid_release(struct r5_vmid_mgr* m, uint32_t owner, uint64_t retired)
{
	uint32_t i;
	if (m == 0 || owner == 0)
		return;
	for (i = 1; i <= 15u; i++) {
		if (m->slot[i].owner != owner)
			continue;
		if (m->slot[i].fence == ~0ull && retired != ~0ull)
			continue;
		if (m->slot[i].fence <= retired)
			m->slot[i].owner = 0;
	}
}

#endif
