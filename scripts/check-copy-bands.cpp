/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host check of the dirty-row bands. Prints one pass line, then the
 * packet streams check-pm4.py reads.
 */

#include "../distro/app_server/GPUCopyBands.h"

#include <stdio.h>

static int gCases = 0;


static void
expect_bands(const char* name, const r5_copy_rows* rows, int count,
	int height, int want, uint32_t y0, uint32_t rows0)
{
	r5_copy_band bands[8];
	int n = r5_copy_bands(rows, count, height, bands, 8);
	if (n != want) {
		fprintf(stderr, "%s bands %d want %d\n", name, n, want);
		return;
	}
	if (want > 0 && (bands[0].y != y0 || bands[0].rows != rows0)) {
		fprintf(stderr, "%s y %u rows %u\n", name, bands[0].y, bands[0].rows);
		return;
	}
	gCases++;
}


static void
print_stream(const char* name, const r5_copy_band* bands, int count,
	uint32_t pitch, uint64_t src, uint64_t dst)
{
	uint32_t words[64];
	int n = r5_copy_bands_pm4(bands, count, pitch, src, dst, words, 64);
	if (n < 0) {
		fprintf(stderr, "%s pm4 failed\n", name);
		return;
	}
	printf("# %s\n", name);
	for (int i = 0; i < n; i++) {
		printf("%08x", words[i]);
		if ((i % 7) == 6 || i + 1 == n)
			printf("\n");
		else
			printf(" ");
	}
	printf("\n");
	gCases++;
}


int
main()
{
	expect_bands("empty", 0, 0, 10, 0, 0, 0);
	r5_copy_rows one = { 3, 3 };
	expect_bands("one row", &one, 1, 10, 1, 3, 1);
	r5_copy_rows full = { 0, 9 };
	expect_bands("full screen", &full, 1, 10, 1, 0, 10);
	r5_copy_rows overlap[2] = { { 0, 2 }, { 1, 4 } };
	expect_bands("overlap", overlap, 2, 10, 1, 0, 5);
	r5_copy_rows gap[2] = { { 0, 1 }, { 4, 5 } };
	r5_copy_band bands[4];
	int n = r5_copy_bands(gap, 2, 10, bands, 4);
	if (n == 2 && bands[0].y == 0 && bands[0].rows == 2
		&& bands[1].y == 4 && bands[1].rows == 2)
		gCases++;
	else
		fprintf(stderr, "gap failed\n");

	r5_copy_band two = { 0, 2 };
	print_stream("two-rows", &two, 1, 16, 0x1000, 0x2000);
	r5_copy_band wide = { 0, 4 };
	print_stream("wide-rows", &wide, 1, 0x100000, 0x80000000ull, 0x90000000ull);

	if (gCases != 7) {
		fprintf(stderr, "cases %d\n", gCases);
		return 1;
	}
	fprintf(stderr, "ok band builder %d cases\n", gCases);
	return 0;
}
