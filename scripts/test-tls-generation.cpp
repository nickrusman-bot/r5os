/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

int
main()
{
	FILE* file = fopen(
		"distro/rest/src/system/runtime_loader/elf_tls.cpp", "rb");
	if (file == NULL)
		return 1;
	char text[65536];
	size_t n = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[n] = 0;
	const char* born = strstr(text, "fGeneration = new Generation");
	const char* counter = born != NULL
		? strstr(born, "SetCounter(TLSBlockTemplates::Get().GetGeneration(-1))")
		: NULL;
	const char* sized = counter != NULL ? strstr(counter, "SetSize(size)") : NULL;
	if (born == NULL || counter == NULL || sized == NULL)
		return 2;
	printf("ok tls generation\n");
	return 0;
}
