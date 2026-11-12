/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

int
main()
{
	FILE* file = fopen("distro/kernel/src/system/kernel/thread.cpp", "rb");
	if (file == NULL)
		return 1;
	static char text[500000];
	size_t n = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[n] = 0;
	const char* mark = strstr(text, "delete the user stack");
	const char* after = mark != NULL
		? strstr(mark, "user_debug_thread_deleted") : NULL;
	if (mark == NULL || after == NULL)
		return 2;
	if (strstr(mark, "exit.initialized") == NULL
		|| strstr(mark, "if (!goingDown)") == NULL)
		return 3;
	printf("ok dying stack\n");
	return 0;
}
