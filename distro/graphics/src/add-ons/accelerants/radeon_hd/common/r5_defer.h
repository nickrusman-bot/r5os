/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Linux v6.12 amdgpu_vm_clear_freed keeps a freed page until the next
 * VM-flush fence has signalled. state 1 is waiting for that fence's
 * sequence. state 2 is waiting for the fence to be seen.
 */

#ifndef R5_DEFER_H
#define R5_DEFER_H

#include <stdint.h>

struct r5_defer {
	uint64_t	page;
	uint64_t	seq;
	int		state;
};


static inline int
r5_defer_add(struct r5_defer* table, int cap, uint64_t page)
{
	int i;
	if (table == 0 || cap <= 0)
		return 0;
	for (i = 0; i < cap; i++) {
		if (table[i].state != 0)
			continue;
		table[i].page = page;
		table[i].seq = 0;
		table[i].state = 1;
		return 1;
	}
	return 0;
}


static inline void
r5_defer_stamp(struct r5_defer* table, int cap, uint64_t seq)
{
	int i;
	if (table == 0)
		return;
	for (i = 0; i < cap; i++) {
		if (table[i].state != 1)
			continue;
		table[i].seq = seq;
		table[i].state = 2;
	}
}


/* 1 and *page set when a stamped page's fence has been seen. */
static inline int
r5_defer_take(struct r5_defer* table, int cap, uint64_t seen, uint64_t* page)
{
	int i;
	if (table == 0 || page == 0)
		return 0;
	for (i = 0; i < cap; i++) {
		if (table[i].state != 2 || table[i].seq > seen)
			continue;
		*page = table[i].page;
		table[i].state = 0;
		table[i].page = 0;
		table[i].seq = 0;
		return 1;
	}
	return 0;
}

#endif
