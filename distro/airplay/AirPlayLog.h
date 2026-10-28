#ifndef R5_AIRPLAY_LOG_H
#define R5_AIRPLAY_LOG_H

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

	FILE* file = fopen("/boot/home/airplay_deskbar.log", "a");
	if (file == NULL)
		file = fopen("/tmp/airplay_deskbar.log", "a");
	if (file != NULL) {
		fprintf(file, "%ld %s: %s\n", (long)time(NULL), who, msg);
		fclose(file);
	}
}

#endif
