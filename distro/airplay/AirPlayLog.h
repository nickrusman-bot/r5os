/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_AIRPLAY_LOG_H
#define R5_AIRPLAY_LOG_H

#include <FindDirectory.h>
#include <Path.h>

#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>


static inline void
r5_airplay_log(const char* who, const char* fmt, ...)
{
	char msg[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s: %s\n", who, msg);

	char logPath[PATH_MAX];
	logPath[0] = '\0';
	BPath dir;
	if (find_directory(B_USER_LOG_DIRECTORY, &dir, true) == B_OK
		&& dir.Path() != NULL) {
		snprintf(logPath, sizeof(logPath), "%s/airplay_deskbar.log",
			dir.Path());
	}

	FILE* file = logPath[0] != '\0' ? fopen(logPath, "a") : NULL;
	if (file == NULL)
		file = fopen("/tmp/airplay_deskbar.log", "a");
	if (file != NULL) {
		fprintf(file, "%ld %s: %s\n", (long)time(NULL), who, msg);
		fclose(file);
	}
}

#endif
