/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host test of the frame compare. No Vulkan.
 */

#include "../distro/apps/vulkanteapot/r5_pixels.c"

#include <stdio.h>
#include <string.h>


static int
fail(const char* text)
{
	fprintf(stderr, "fail %s\n", text);
	return 1;
}


int
main(void)
{
	uint8_t a[16];
	uint8_t b[16];
	int percent = -1;
	int delta = -1;
	memset(a, 10, sizeof(a));
	memset(b, 10, sizeof(b));
	if (r5_pixels_compare(a, b, 4, 2, &percent, &delta) != 0)
		return fail("equal");
	if (percent != 0 || delta != 0)
		return fail("equal numbers");
	b[5] = 12;
	if (r5_pixels_compare(a, b, 4, 2, &percent, &delta) != 0)
		return fail("inside tolerance");
	if (delta != 2)
		return fail("delta 2");
	b[5] = 13;
	if (r5_pixels_compare(a, b, 4, 2, &percent, &delta) == 0)
		return fail("over tolerance");
	if (percent != 250 || delta != 3)
		return fail("one pixel");
	uint8_t manyA[400];
	uint8_t manyB[400];
	memset(manyA, 10, sizeof(manyA));
	memset(manyB, 10, sizeof(manyB));
	manyB[0] = 13;
	if (r5_pixels_compare(manyA, manyB, 100, 2, &percent, &delta) != 0)
		return fail("one percent");
	if (percent != 10)
		return fail("one percent share");
	manyB[4] = 13;
	if (r5_pixels_compare(manyA, manyB, 100, 2, &percent, &delta) == 0)
		return fail("two percent");
	memset(b, 0, sizeof(b));
	if (r5_pixels_compare(a, b, 4, 2, &percent, &delta) == 0)
		return fail("all");
	if (percent != 1000)
		return fail("percent");
	printf("ok pixels\n");
	return 0;
}
