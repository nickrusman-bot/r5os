/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

int
main()
{
	FILE* file = fopen("distro/kernel/src/system/kernel/vm/vm.cpp", "rb");
	if (file == NULL)
		return 1;
	static char text[700000];
	size_t n = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[n] = 0;
	const char* first = strstr(text, "is the first in this area");
	const char* shrunk = first != NULL
		? strstr(first, "range->size = nextAddress - areaStart") : NULL;
	const char* revert = shrunk != NULL
		? strstr(shrunk, "unwire all that") : NULL;
	if (first == NULL || shrunk == NULL || revert == NULL)
		return 2;
	if (strstr(text, "range->size -= areaEnd - nextAddress") != NULL)
		return 3;
	printf("ok wired range\n");
	return 0;
}
