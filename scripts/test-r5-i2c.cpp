/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host check for the I2C child walk and the STOP_DET wait.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r5_i2c_scan.h"


static int
fail(const char* what)
{
	fprintf(stderr, "fail %s\n", what);
	return 1;
}


static int
snippet(const char* path, const char* begin, const char* end, char* out,
	size_t cap)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return fail(path);
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return fail(path);
	}
	long size = ftell(file);
	if (size < 0 || size > 2000000) {
		fclose(file);
		return fail(path);
	}
	rewind(file);
	char* text = (char*)malloc((size_t)size + 1);
	if (text == NULL) {
		fclose(file);
		return fail(path);
	}
	if (fread(text, 1, (size_t)size, file) != (size_t)size) {
		free(text);
		fclose(file);
		return fail(path);
	}
	text[size] = 0;
	fclose(file);
	const char* from = strstr(text, begin);
	const char* to = from == NULL ? NULL : strstr(from, end);
	if (from == NULL || to == NULL || (size_t)(to - from) >= cap) {
		free(text);
		return fail(begin);
	}
	memcpy(out, from, (size_t)(to - from));
	out[to - from] = 0;
	free(text);
	return 0;
}


int
main()
{
	if (r5_i2c_child_continue(5, 0) != 0)
		return fail("failed child stops");
	if (r5_i2c_child_continue(0, 1) != 0)
		return fail("missing address stops");
	if (r5_i2c_child_continue(0, 0) != 0)
		return fail("good child");
	if (r5_i2c_stop_wait_again(1) != 1)
		return fail("stop arrived");
	if (r5_i2c_stop_wait_again(0) != 0)
		return fail("stop timeout waits again");

	char text[4000];
	const char* path = "distro/rest/src/add-ons/kernel/busses/i2c/pch/"
		"pch_i2c.cpp";
	if (snippet(path, "Could not find i2c address", "deviceAddress",
			text, sizeof(text)) != 0)
		return fail("address span");
	if (strstr(text, "r5_i2c_child_continue") == NULL
		|| strstr(text, "return B_BAD_DATA") != NULL)
		return fail("address still stops");
	if (snippet(path, "timed out waiting for busy", "processing done",
			text, sizeof(text)) != 0)
		return fail("stop span");
	if (strstr(text, "break;") == NULL)
		return fail("stop timeout loops");

	printf("ok i2c scan\n");
	return 0;
}
