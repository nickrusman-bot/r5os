/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * VMIDs 1-15, one owner at a time. Linux v6.18 amdgpu_ids.c
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
	/* tlb_seq of this owner at the last flush of this VMID.
	 * Linux v6.12 amdgpu_ids.c amdgpu_vmid_grab compares vm->tlb_seq
	 * with the id's flushed sequence and flushes the same owner when
	 * they differ. */
	uint64_t	flushed;
};

struct r5_vmid_mgr {
	struct r5_vmid_slot	slot[16];
	uint32_t			lru[15];
	uint32_t			chosen;
	uint32_t			hold;
	uint32_t			hold_owner_prev;
	uint64_t			hold_fence_prev;
	uint64_t			pending_tlb;
	/* Lowest VMID this manager may hand out. 0 means 1-15.
	 * A file manager sets 2. VMID 1 stays the shared table. */
	uint32_t			low;
};


static inline void
r5_vmid_init(struct r5_vmid_mgr* m)
{
	uint32_t i;
	memset(m, 0, sizeof(*m));
	for (i = 0; i < 15u; i++)
		m->lru[i] = i + 1u;
}


/* VMID 1 is the shared table. Files use 2-15 only. */
static inline void
r5_vmid_init_files(struct r5_vmid_mgr* m)
{
	uint32_t i;
	r5_vmid_init(m);
	m->low = 2u;
	for (i = 0; i < 14u; i++)
		m->lru[i] = i + 2u;
	m->lru[14] = 0;
}


/* gfx9_vmid_per_file needs the VMID 1-15 context registers. */
static inline int
r5_vmid_file_ok(int perFile, int vmids)
{
	return perFile != 0 && vmids != 0;
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
	uint64_t tlb, int* flush)
{
	uint32_t i;
	if (m == 0 || flush == 0 || owner == 0 || m->hold != 0)
		return 0;
	m->pending_tlb = tlb;
	for (i = 1; i <= 15u; i++) {
		if (i < m->low)
			continue;
		if (m->slot[i].owner != owner)
			continue;
		if (m->slot[i].fence == ~0ull || m->slot[i].fence <= retired)
			continue;
		*flush = m->slot[i].flushed != tlb ? 1 : 0;
		m->chosen = i;
		r5_vmid_touch(m, i);
		return i;
	}
	for (i = 0; i < 15u; i++) {
		uint32_t v = m->lru[i];
		if (v < m->low || v > 15u)
			continue;
		if (m->slot[v].fence == ~0ull)
			continue;
		if (m->slot[v].fence > retired)
			continue;
		*flush = (m->slot[v].owner != owner || m->slot[v].flushed != tlb)
			? 1 : 0;
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
	m->slot[m->chosen].flushed = m->pending_tlb;
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

/* After a GPU reset. The chip forgot every VMID's page table, so each
 * slot flushes on its next use. Owners and fences stay: a file that
 * still holds a tree releases only its own slots later, and a slot
 * given to a new owner is never cleared by the old one. A reservation
 * that was held is dropped. */
static inline void
r5_vmid_after_reset(struct r5_vmid_mgr* m)
{
	uint32_t i;
	if (m == 0)
		return;
	r5_vmid_cancel(m);
	for (i = 1; i <= 15u; i++)
		m->slot[i].flushed = ~0ull;
}

#endif
