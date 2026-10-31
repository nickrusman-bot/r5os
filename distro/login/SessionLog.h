/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_SESSION_LOG_H
#define R5_SESSION_LOG_H


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>


static inline void
r5_session_log(const char* format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);

	static const char* kPaths[] = {
		"/tmp/r5-session.log",
		"/boot/system/var/log/r5-session.log",
		NULL
	};
	for (int i = 0; kPaths[i] != NULL; i++) {
		FILE* file = fopen(kPaths[i], "a");
		if (file == NULL)
			continue;
		fprintf(file, "%lld uid=%d euid=%d %s\n",
			(long long)system_time(), (int)getuid(), (int)geteuid(), line);
		fclose(file);
	}
}


#endif	// R5_SESSION_LOG_H
