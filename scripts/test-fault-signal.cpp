/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

static int
load(const char* path, char* text, size_t cap)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return 0;
	size_t n = fread(text, 1, cap - 1, file);
	fclose(file);
	text[n] = 0;
	return 1;
}

int
main()
{
	static char text[700000];
	if (!load("distro/kernel/src/system/kernel/signal.cpp", text, sizeof(text)))
		return 1;
	char* fn = strstr(text, "send_fault_signal(");
	char* next = fn != NULL
		? strstr(fn, "Sends the given signal to the thread with") : NULL;
	if (fn == NULL || next == NULL)
		return 2;
	*next = 0;
	if (strstr(fn, "action.sa_handler = SIG_DFL") == NULL
		|| strstr(fn, "sig_block_mask") == NULL)
		return 3;
	if (!load("distro/kernel/src/system/kernel/vm/vm.cpp", text, sizeof(text)))
		return 3;
	const char* addr = strstr(text, "signal.SetAddress((void*)address)");
	const char* call = addr != NULL ? strstr(addr, "send_fault_signal(thread, signal)") : NULL;
	const char* done = call != NULL ? strstr(call, "return B_HANDLED_INTERRUPT") : NULL;
	if (addr == NULL || call == NULL || done == NULL)
		return 4;
	printf("ok fault signal\n");
	return 0;
}
