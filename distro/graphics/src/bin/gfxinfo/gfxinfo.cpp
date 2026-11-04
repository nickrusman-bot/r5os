/*
 * Copyright 2026, R5OS Contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Prints the GFX9 boot record written by the radeon_hd accelerant.
 */


#include <FindDirectory.h>
#include <SupportDefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char* kKeys[] = {
	"device",
	"psp",
	"rlc",
	"rlc_before",
	"rlc_after",
	"rlc_version",
	"scratch",
	"packet",
	"dwords",
	"rptr",
	"wptr",
};


static bool
read_file(const char* path, char* buffer, size_t capacity)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;
	size_t got = fread(buffer, 1, capacity - 1, file);
	fclose(file);
	buffer[got] = '\0';
	return got > 0;
}


static const char*
value_of(const char* text, const char* key)
{
	static char value[64];
	size_t keyLength = strlen(key);
	const char* line = text;
	while (*line != '\0') {
		if (strncmp(line, key, keyLength) == 0 && line[keyLength] == ' ') {
			const char* start = line + keyLength + 1;
			size_t length = 0;
			while (start[length] != '\0' && start[length] != '\n'
				&& length + 1 < sizeof(value)) {
				value[length] = start[length];
				length++;
			}
			value[length] = '\0';
			return value;
		}
		const char* next = strchr(line, '\n');
		if (next == NULL)
			break;
		line = next + 1;
	}
	return NULL;
}


int
main()
{
	char path[B_PATH_NAME_LENGTH];
	char text[2048];
	bool fromCache = false;
	if (find_directory(B_SYSTEM_CACHE_DIRECTORY, -1, false, path,
			sizeof(path)) == B_OK) {
		size_t used = strlen(path);
		if (used + strlen("/radeon-gfx.txt") < sizeof(path)) {
			strcat(path, "/radeon-gfx.txt");
			fromCache = read_file(path, text, sizeof(text));
		}
	}

	if (!fromCache) {
		printf("gfxinfo: no /boot/system/cache/radeon-gfx.txt\n");
		printf("gfxinfo: the accelerant writes that file during boot\n");
		return 2;
	}

	printf("gfxinfo: %s\n", path);
	for (uint32 i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
		const char* value = value_of(text, kKeys[i]);
		if (value != NULL)
			printf("  %s %s\n", kKeys[i], value);
	}

	const char* packet = value_of(text, "packet");
	if (packet != NULL && strcmp(packet, "yes") == 0) {
		printf("gfxinfo: the command processor ran the test packet\n");
		return 0;
	}
	printf("gfxinfo: the command processor did not run the test packet\n");
	return 1;
}
