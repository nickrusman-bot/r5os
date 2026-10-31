/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * R5OS home modes. Other accounts must not read private files.
 *
 * /boot/home stays traversable (0711) because extra homes live in
 * /boot/home/users/<name>. Private names under /boot/home are 0700/0600.
 * After Sign in, the first account is a person uid, so these bits apply.
 */


#include "HomeLock.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>


static const char* kHome = "/boot/home";
static const char* kUsers = "/boot/home/users";


static void
lock_entry(const char* path, mode_t mode, dev_t homeDevice)
{
	struct stat st;
	if (lstat(path, &st) != 0)
		return;
	if (S_ISLNK(st.st_mode))
		return;
	if (st.st_dev != homeDevice)
		return;
	chmod(path, mode);
}



status_t
lock_private_homes()
{
	struct stat homeStat;
	if (lstat(kHome, &homeStat) != 0 || !S_ISDIR(homeStat.st_mode))
		return errno != 0 ? errno : B_ENTRY_NOT_FOUND;

	mkdir(kUsers, 0755);
	chmod(kHome, 0711);

	DIR* dir = opendir(kHome);
	if (dir == NULL)
		return errno;

	while (struct dirent* entry = readdir(dir)) {
		if (strcmp(entry->d_name, ".") == 0
			|| strcmp(entry->d_name, "..") == 0
			|| strcmp(entry->d_name, "users") == 0)
			continue;

		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", kHome, entry->d_name);

		struct stat st;
		if (lstat(path, &st) != 0 || S_ISLNK(st.st_mode))
			continue;
		lock_entry(path, S_ISDIR(st.st_mode) ? 0700 : 0600, homeStat.st_dev);
	}
	closedir(dir);

	chmod(kUsers, 0755);

	dir = opendir(kUsers);
	if (dir == NULL)
		return B_OK;

	while (struct dirent* entry = readdir(dir)) {
		if (entry->d_name[0] == '.')
			continue;

		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", kUsers, entry->d_name);
		lock_entry(path, 0700, homeStat.st_dev);
	}
	closedir(dir);
	return B_OK;
}
