/*
 * Copyright 2026, R5OS Contributors.
 * SPDX-License-Identifier: MIT
 */


//!	CodyCam's own notes. printf from a Deskbar launch never reaches the
//!	syslog the stick reader copies, so the same line goes there and to
//!	/boot/home/codycam.log.


#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>


static inline void
codycam_log(const char* format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);

	size_t length = strlen(line);
	while (length > 0 && line[length - 1] == '\n')
		line[--length] = '\0';

	syslog(LOG_INFO, "CodyCam: %s", line);

	int fd = open("/boot/home/codycam.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		return;
	write(fd, line, length);
	write(fd, "\n", 1);
	close(fd);
}
